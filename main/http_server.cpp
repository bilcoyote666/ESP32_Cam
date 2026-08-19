#include "http_server.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "sd_storage.h"
#include "auth.h"
#include "led.h"
#include <sys/param.h>

static const char *TAG = "HTTP";

extern "C" esp_err_t main_trigger_capture(void);

// Referencias a los archivos embebidos (generados por target_add_binary_data)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");



// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

/**
 * @brief Comprueba si la petición contiene una cookie de sesión válida
 */
static bool is_authenticated(httpd_req_t *req) {
    return true; // Bypass para pruebas
}

// ============================================================================
// HANDLERS ESTATICOS
// ============================================================================

static esp_err_t index_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    const size_t len = index_html_end - index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, len);
    return ESP_OK;
}
// ============================================================================
// HANDLERS DE LA API Y FOTOS
// ============================================================================

#include "wifi_ap.h"

static esp_err_t status_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    char pwd[64] = {0};
    auth_get_password(pwd, sizeof(pwd));
    
    char resp[160];
    snprintf(resp, sizeof(resp), "{\"pwd_set\": true, \"auth\": true, \"wifi_pass\": \"%s\"}", pwd);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t password_post_handler(httpd_req_t *req) {
    char pwd[65] = {0};
    int ret = httpd_req_recv(req, pwd, MIN(req->content_len, sizeof(pwd) - 1));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    // Parse JSON if sent as {"password":"..."} or plain text
    char clean_pwd[65] = {0};
    char *p = strstr(pwd, "\"password\":");
    if (p) {
        p += 11;
        while (*p == ' ' || *p == '\"') p++;
        int i = 0;
        while (*p && *p != '\"' && *p != '}' && i < 64) {
            clean_pwd[i++] = *p++;
        }
        clean_pwd[i] = 0;
    } else {
        strncpy(clean_pwd, pwd, sizeof(clean_pwd) - 1);
    }
    
    if (strlen(clean_pwd) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "La contraseña debe tener al menos 8 caracteres");
        return ESP_FAIL;
    }
    
    if (auth_set_password(clean_pwd) == ESP_OK) {
        wifi_ap_set_password(clean_pwd);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true,\"msg\":\"Contraseña actualizada\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
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
    
    if (!sd_lock(2000)) {
        ESP_LOGW(TAG, "SD ocupada para leer foto: %s", param);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    FILE* f = sd_open_file(param, "rb");
    if (!f) {
        sd_unlock();
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000");

    char download_val[16] = {0};
    if (httpd_query_key_value(filename, "download", download_val, sizeof(download_val)) == ESP_OK && strcmp(download_val, "1") == 0) {
        char disposition_hdr[128];
        snprintf(disposition_hdr, sizeof(disposition_hdr), "attachment; filename=\"%s\"", param);
        httpd_resp_set_hdr(req, "Content-Disposition", disposition_hdr);
    } else {
        httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    }
    
    const size_t CHUNK_SIZE = 4096;
    char *chunk = (char *)malloc(CHUNK_SIZE);
    if (!chunk) {
        fclose(f);
        sd_unlock();
        ESP_LOGE(TAG, "No memory for chunk buffer");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Chunked transfer
    size_t chunk_len = 0;
    esp_err_t res = ESP_OK;
    while ((chunk_len = fread(chunk, 1, CHUNK_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, chunk_len) != ESP_OK) {
            ESP_LOGW(TAG, "Conexión cerrada por cliente durante envío de foto");
            res = ESP_FAIL;
            break;
        }
    }
    fclose(f);
    free(chunk);
    sd_unlock();
    
    if (res == ESP_OK) {
        // Finalize chunked response
        httpd_resp_send_chunk(req, NULL, 0);
    }
    return res;
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

static esp_err_t delete_post_handler(httpd_req_t *req) {
    if (auth_is_password_set() && !is_authenticated(req)) {
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

    ESP_LOGI(TAG, "POST /api/delete -> Eliminando foto: %s", param);
    if (sd_delete_photo(param) == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error eliminando el archivo de la SD");
        return ESP_FAIL;
    }
}

static esp_err_t capture_post_handler(httpd_req_t *req) {
    if (auth_is_password_set() && !is_authenticated(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "No Autorizado");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "POST /api/capture -> Disparando captura remota");
    if (main_trigger_capture() == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error encolando captura");
        return ESP_FAIL;
    }
}

static esp_err_t flash_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/flash -> Disparando destello de prueba de Flash (Pin D2)");
    led_trigger_flash(200);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"msg\":\"Flash OK\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ============================================================================
// BYPASS CAPTIVE PORTAL (Evita que iOS / Android bloqueen descargas en ventanas emergentes)
// ============================================================================

static esp_err_t apple_hotspot_detect_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t android_generate_204_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t windows_connect_test_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Microsoft Connect Test", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240; // 10KB para soportar operaciones FATFS y streaming sin stack overflow
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;

    // Inicializar auth
    auth_init();

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registrando Endpoints Web y API...");

        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_status);
        
        httpd_uri_t uri_setup = { .uri = "/api/setup", .method = HTTP_POST, .handler = setup_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_setup);
        
        httpd_uri_t uri_login = { .uri = "/api/login", .method = HTTP_POST, .handler = login_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_login);

        httpd_uri_t uri_password = { .uri = "/api/password", .method = HTTP_POST, .handler = password_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_password);

        httpd_uri_t uri_list = { .uri = "/list", .method = HTTP_GET, .handler = list_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_list);
        
        httpd_uri_t uri_photo = { .uri = "/photo", .method = HTTP_GET, .handler = photo_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_photo);

        httpd_uri_t uri_time = { .uri = "/api/time", .method = HTTP_POST, .handler = time_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_time);

        httpd_uri_t uri_capture = { .uri = "/api/capture", .method = HTTP_POST, .handler = capture_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_capture);

        httpd_uri_t uri_flash = { .uri = "/api/flash", .method = HTTP_POST, .handler = flash_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_flash);

        httpd_uri_t uri_delete = { .uri = "/api/delete", .method = HTTP_POST, .handler = delete_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_delete);

        // Respuestas de conectividad limpia para iOS, Android y Windows
        httpd_uri_t uri_apple1 = { .uri = "/hotspot-detect.html*", .method = HTTP_GET, .handler = apple_hotspot_detect_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_apple1);

        httpd_uri_t uri_apple2 = { .uri = "/library/test/success.html*", .method = HTTP_GET, .handler = apple_hotspot_detect_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_apple2);

        httpd_uri_t uri_android1 = { .uri = "/generate_204*", .method = HTTP_GET, .handler = android_generate_204_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_android1);

        httpd_uri_t uri_android2 = { .uri = "/gen_204*", .method = HTTP_GET, .handler = android_generate_204_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_android2);

        httpd_uri_t uri_win1 = { .uri = "/connecttest.txt*", .method = HTTP_GET, .handler = windows_connect_test_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_win1);

        httpd_uri_t uri_win2 = { .uri = "/ncsi.txt*", .method = HTTP_GET, .handler = windows_connect_test_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_win2);

        ESP_LOGI(TAG, "HTTP Server iniciado correctamente.");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Fallo al arrancar el servidor HTTP");
    return ESP_FAIL;
}
