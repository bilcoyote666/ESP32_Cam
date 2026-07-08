#include "http_server.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "sd_storage.h"
#include "auth.h"
#include <sys/param.h>

static const char *TAG = "HTTP";

// Referencias a los archivos embebidos (generados por target_add_binary_data)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[]   asm("_binary_styles_css_end");

extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

/**
 * @brief Comprueba si la petición contiene una cookie de sesión válida
 */
static bool is_authenticated(httpd_req_t *req) {
    if (!auth_is_password_set()) {
        return false; // No hay contraseña configurada
    }
    
    char cookie_hdr[256];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_hdr, sizeof(cookie_hdr)) == ESP_OK) {
        // Buscar "cam_session="
        char *session = strstr(cookie_hdr, AUTH_SESSION_COOKIE_NAME "=");
        if (session) {
            session += strlen(AUTH_SESSION_COOKIE_NAME "=");
            // El token llega hasta el final, o hasta el punto y coma
            char token[64] = {0};
            int i = 0;
            while (session[i] != '\0' && session[i] != ';' && i < sizeof(token) - 1) {
                token[i] = session[i];
                i++;
            }
            token[i] = '\0';
            
            return auth_verify_session(token);
        }
    }
    return false;
}

// ============================================================================
// HANDLERS ESTATICOS
// ============================================================================

static esp_err_t index_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /");
    httpd_resp_set_type(req, "text/html");
    const size_t len = index_html_end - index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, len);
    return ESP_OK;
}

static esp_err_t styles_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    const size_t len = styles_css_end - styles_css_start;
    httpd_resp_send(req, (const char *)styles_css_start, len);
    return ESP_OK;
}

static esp_err_t app_js_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    const size_t len = app_js_end - app_js_start;
    httpd_resp_send(req, (const char *)app_js_start, len);
    return ESP_OK;
}

// ============================================================================
// HANDLERS DE LA API Y FOTOS
// ============================================================================

static esp_err_t status_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    bool pwd_set = auth_is_password_set();
    bool auth = pwd_set ? is_authenticated(req) : false;
    
    char resp[100];
    snprintf(resp, sizeof(resp), "{\"pwd_set\": %s, \"auth\": %s}", 
             pwd_set ? "true" : "false", 
             auth ? "true" : "false");
             
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t setup_post_handler(httpd_req_t *req) {
    if (auth_is_password_set()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ya configurado");
        return ESP_FAIL;
    }
    
    char pwd[65] = {0};
    int ret = httpd_req_recv(req, pwd, MIN(req->content_len, sizeof(pwd) - 1));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    if (auth_set_password(pwd) == ESP_OK) {
        // Enviar la cookie
        char cookie_str[128];
        snprintf(cookie_str, sizeof(cookie_str), "%s=%s; Path=/; HttpOnly", AUTH_SESSION_COOKIE_NAME, auth_get_session_token());
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_str);
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static esp_err_t login_post_handler(httpd_req_t *req) {
    if (!auth_is_password_set()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No hay pass");
        return ESP_FAIL;
    }
    
    char pwd[65] = {0};
    int ret = httpd_req_recv(req, pwd, MIN(req->content_len, sizeof(pwd) - 1));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    if (auth_verify_password(pwd)) {
        char cookie_str[128];
        snprintf(cookie_str, sizeof(cookie_str), "%s=%s; Path=/; HttpOnly", AUTH_SESSION_COOKIE_NAME, auth_get_session_token());
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_str);
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    return ESP_FAIL;
}

static esp_err_t list_get_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "No Autorizado");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GET /list");
    httpd_resp_set_type(req, "application/json");
    
    char *json_response = sd_list_files_json();
    if (json_response) {
        httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
        free(json_response);
    } else {
        httpd_resp_send_500(req);
    }
    
    return ESP_OK;
}

static esp_err_t photo_get_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "No Autorizado");
        return ESP_FAIL;
    }
    
    char filename[128] = {0};
    if (httpd_req_get_url_query_str(req, filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta query string");
        return ESP_FAIL;
    }
    
    char param[64] = {0};
    if (httpd_query_key_value(filename, "name", param, sizeof(param)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta parametro name");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GET /photo %s", param);
    
    FILE* f = sd_open_file(param, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    
    // Chunked transfer
    char chunk[8192];
    size_t chunk_len = 0;
    while ((chunk_len = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, chunk_len) != ESP_OK) {
            fclose(f);
            ESP_LOGE(TAG, "Error enviando chunk");
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    
    // Finalize
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t time_post_handler(httpd_req_t *req) {
    char content[32];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Configurar la hora del sistema
    long unsigned int ts = strtoul(content, NULL, 10);
    if (ts > 0) {
        struct timeval tv;
        tv.tv_sec = ts;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Hora del sistema sincronizada via HTTP: %lu", ts);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ============================================================================
// INICIALIZACION
// ============================================================================

// Handler para el Captive Portal (Redirigir todo lo no encontrado a la IP principal)
static esp_err_t captive_portal_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;

    // Inicializar auth
    auth_init();

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registrando Endpoints Web y API...");

        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_root);
        
        httpd_uri_t uri_css = { .uri = "/styles.css", .method = HTTP_GET, .handler = styles_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_css);
        
        httpd_uri_t uri_js = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_js);
        
        httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_status);
        
        httpd_uri_t uri_setup = { .uri = "/api/setup", .method = HTTP_POST, .handler = setup_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_setup);
        
        httpd_uri_t uri_login = { .uri = "/api/login", .method = HTTP_POST, .handler = login_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_login);

        httpd_uri_t uri_list = { .uri = "/list", .method = HTTP_GET, .handler = list_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_list);
        
        httpd_uri_t uri_photo = { .uri = "/photo", .method = HTTP_GET, .handler = photo_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_photo);

        httpd_uri_t uri_time = { .uri = "/api/time", .method = HTTP_POST, .handler = time_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_time);

        ESP_LOGI(TAG, "HTTP Server iniciado correctamente.");

        // Registrar error handler 404 para el captive portal
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_portal_handler);

        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Fallo al arrancar el servidor HTTP");
    return ESP_FAIL;
}
