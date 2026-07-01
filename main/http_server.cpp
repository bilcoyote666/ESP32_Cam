#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sd_storage.h"
#include "config.h"
#include <sys/param.h>
#include <sys/time.h>
#include "auth.h"

// Helper para comprobar la autenticación
static bool is_authenticated(httpd_req_t *req) {
    if (!auth_is_password_set()) return true; // Si no hay contraseña, acceso libre (para poder configurarla)
    
    char cookie_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_header, sizeof(cookie_header)) == ESP_OK) {
        const char* expected_token = auth_get_session_token();
        // Buscamos si la cookie esperada está en el header
        char expected_cookie[64];
        snprintf(expected_cookie, sizeof(expected_cookie), "%s=%s", AUTH_SESSION_COOKIE_NAME, expected_token);
        if (strstr(cookie_header, expected_cookie) != NULL) {
            return true;
        }
    }
    return false;
}


static const char *TAG = "HTTP_SERVER";

// Archivos embebidos generados por CMake
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[]   asm("_binary_styles_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// ============================================================================
// Rutas de Archivos Estáticos (Frontend)
// ============================================================================

static esp_err_t index_html_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    const size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

static esp_err_t styles_css_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    const size_t len = styles_css_end - styles_css_start;
    return httpd_resp_send(req, (const char *)styles_css_start, len);
}

static esp_err_t app_js_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    const size_t len = app_js_end - app_js_start;
    return httpd_resp_send(req, (const char *)app_js_start, len);
}

// ============================================================================
// Rutas Captive Portal (Redirecciones a /)
// ============================================================================
static esp_err_t captive_portal_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t err_404_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================================================================
// Rutas de Autenticación y Sistema
// ============================================================================

static esp_err_t api_time_post_handler(httpd_req_t *req) {
    char buf[32] = {0};
    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (ret > 0) {
        long timestamp = atol(buf);
        if (timestamp > 0) {
            struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Hora sincronizada con el cliente: %ld", timestamp);
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_status_get_handler(httpd_req_t *req) {
    bool has_pwd = auth_is_password_set();
    bool is_auth = is_authenticated(req);
    char json[128];
    snprintf(json, sizeof(json), "{\"pwd_set\":%s,\"auth\":%s}", 
             has_pwd ? "true" : "false", 
             is_auth ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_setup_post_handler(httpd_req_t *req) {
    if (auth_is_password_set()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password already set");
        return ESP_FAIL;
    }
    
    char pwd[AUTH_MAX_PASSWORD_LEN] = {0};
    int ret = httpd_req_recv(req, pwd, MIN(req->content_len, sizeof(pwd) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No password provided");
        return ESP_FAIL;
    }
    
    if (auth_set_password(pwd) == ESP_OK) {
        httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static esp_err_t api_login_post_handler(httpd_req_t *req) {
    char pwd[AUTH_MAX_PASSWORD_LEN] = {0};
    int ret = httpd_req_recv(req, pwd, MIN(req->content_len, sizeof(pwd) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No password provided");
        return ESP_FAIL;
    }
    
    if (auth_verify_password(pwd)) {
        // Establecer cookie
        char cookie_header[128];
        snprintf(cookie_header, sizeof(cookie_header), "%s=%s; Path=/; HttpOnly", 
                 AUTH_SESSION_COOKIE_NAME, auth_get_session_token());
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);
        httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid password");
    return ESP_FAIL;
}


// ============================================================================
// Rutas de la API (SD Card)
// ============================================================================

static esp_err_t list_get_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
    }
    char* json = sd_list_files_json();
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"Error reading SD\"}", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return res;
}

static esp_err_t photo_get_handler(httpd_req_t *req) {
    if (auth_is_password_set() && !is_authenticated(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[128];
    char filename[SD_MAX_FILENAME_LEN] = {0};
    
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "name", filename, sizeof(filename)) != ESP_OK) {
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE* f = sd_open_file(filename, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600"); // Las fotos sí se pueden cachear
    
    // Enviar el archivo en chunks
    char chunk[1024];
    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, sizeof(chunk), f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(f);
                return ESP_FAIL;
            }
        }
    } while (read_bytes == sizeof(chunk));
    
    fclose(f);
    // Finalizar el chunked transfer
    return httpd_resp_send_chunk(req, NULL, 0);
}

// ============================================================================
// Inicialización del Servidor
// ============================================================================

esp_err_t http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Captive portal suele lanzar muchas peticiones simultáneas (Apple, Android...)
    config.max_open_sockets = 7;
    // Capturamos cualquier error 404 para redirigir al portal cautivo
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        auth_init(); // Inicializar autenticación

        // Estáticos
        httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = index_html_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_css   = { .uri = "/styles.css", .method = HTTP_GET, .handler = styles_css_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_js    = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler, .user_ctx = NULL };
        
        // Auth & System API
        httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_setup  = { .uri = "/api/setup", .method = HTTP_POST, .handler = api_setup_post_handler, .user_ctx = NULL };
        httpd_uri_t uri_login  = { .uri = "/api/login", .method = HTTP_POST, .handler = api_login_post_handler, .user_ctx = NULL };
        httpd_uri_t uri_time   = { .uri = "/api/time", .method = HTTP_POST, .handler = api_time_post_handler, .user_ctx = NULL };

        // API
        httpd_uri_t uri_list  = { .uri = "/list", .method = HTTP_GET, .handler = list_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_photo = { .uri = "/photo", .method = HTTP_GET, .handler = photo_get_handler, .user_ctx = NULL };
        
        // Android captive portal detection
        httpd_uri_t uri_204   = { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_handler, .user_ctx = NULL };
        // Captive portal catch-all handled by 404 handler instead

        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_css);
        httpd_register_uri_handler(server, &uri_js);
        
        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_setup);
        httpd_register_uri_handler(server, &uri_login);
        httpd_register_uri_handler(server, &uri_time);

        httpd_register_uri_handler(server, &uri_list);
        httpd_register_uri_handler(server, &uri_photo);
        httpd_register_uri_handler(server, &uri_204);
        // El handler 404 se encarga del resto
        // Registrar el manejador de 404 para cualquier otra ruta (por ejemplo /hotspot-detect.html de iOS)
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, err_404_handler);

        ESP_LOGI(TAG, "HTTP Server y Captive Portal iniciados");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Fallo al arrancar el servidor HTTP");
    return ESP_FAIL;
}
