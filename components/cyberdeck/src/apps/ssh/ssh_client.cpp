#include "apps/ssh/ssh_client.h"
#include "cyberdeck_paths.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <libssh/libssh.h>

#include <arpa/inet.h>
#include <string>
#include <vector>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <atomic>
#include <mutex>

static const char *TAG = "tab5_ssh";

namespace {

std::string ssh_connect_error(ssh_session session, int rc)
{
    const char *error = ssh_get_error(session);
    std::string message = error != nullptr ? error : "erro desconhecido";

    // libssh 0.12.2 formats getnameinfo failures using errno.  getnameinfo
    // returns EAI_* directly and does not promise to set errno, so errno==0
    // otherwise becomes the misleading string "Success".  The actual
    // failure is still handled by libssh; only its user-facing diagnostic is
    // corrected here, without modifying the managed dependency.
    const std::string suffix = "getnameinfo failed: Success";
    const size_t position = message.find(suffix);
    if (position != std::string::npos) {
        message.replace(position, suffix.size(), "getnameinfo failed (address formatting error)");
    }
    // Keep libssh's return code visible to callers.  In particular, do not
    // replace it with the value from a subsequent diagnostic operation.
    message += " (rc=" + std::to_string(rc) + ")";
    return message;
}

int ssh_address_family_for_host(const char *host)
{
    struct in_addr ipv4{};
    if (inet_pton(AF_INET, host, &ipv4) == 1) {
        return SSH_ADDRESS_FAMILY_INET;
    }

    struct in6_addr ipv6{};
    if (inet_pton(AF_INET6, host, &ipv6) == 1) {
        return SSH_ADDRESS_FAMILY_INET6;
    }

    return SSH_ADDRESS_FAMILY_ANY;
}

struct SSHConfig {
    std::string user;
    std::string host;
    int port{22};
};

std::atomic<ssh_client_state_t> s_state{SSH_CLIENT_DISCONNECTED};
std::atomic<ssh_client_generation_t> s_generation{0};
ssh_rx_cb_t s_rx_cb = nullptr;
ssh_state_cb_t s_state_cb = nullptr;

std::atomic<TaskHandle_t> s_task_handle{nullptr};
QueueHandle_t s_input_queue = nullptr;
QueueHandle_t s_password_queue = nullptr;
QueueHandle_t s_host_key_queue = nullptr;
SemaphoreHandle_t s_state_mutex = nullptr;

std::atomic_bool s_stop_requested{false};
SSHConfig s_current_config;
static std::once_flag s_ssh_init_once;

void set_state_locked(ssh_client_generation_t generation, ssh_client_state_t new_state, const char *msg = nullptr)
{
    s_state.store(new_state, std::memory_order_release);
    ssh_state_cb_t cb = s_state_cb;
    if (cb != nullptr) {
        cb(generation, new_state, msg);
    }
}

void dispatch_rx(ssh_client_generation_t generation, const char *data, size_t len)
{
    ssh_rx_cb_t cb = s_rx_cb;
    if (cb != nullptr && len > 0) {
        cb(generation, data, len);
    }
}

bool verify_host_key(ssh_client_generation_t generation, ssh_session session)
{
    const enum ssh_known_hosts_e host_state = ssh_session_is_known_server(session);
    if (host_state == SSH_KNOWN_HOSTS_OK) {
        return true;
    }
    if (host_state == SSH_KNOWN_HOSTS_UNKNOWN || host_state == SSH_KNOWN_HOSTS_NOT_FOUND) {
        set_state_locked(generation, SSH_CLIENT_NEED_HOST_KEY, "Chave de host desconhecida. Confirme para continuar.");
        uint8_t accepted = 0;
        while (!s_stop_requested.load(std::memory_order_acquire)) {
            if (xQueueReceive(s_host_key_queue, &accepted, pdMS_TO_TICKS(100)) == pdTRUE && accepted != 0) {
                if (ssh_session_update_known_hosts(session) != SSH_OK) {
                    set_state_locked(generation, SSH_CLIENT_ERROR, "Nao foi possivel salvar a chave do host.");
                    return false;
                }
                return true;
            }
        }
        set_state_locked(generation, SSH_CLIENT_DISCONNECTED, "Chave de host recusada.");
        return false;
    }
    set_state_locked(generation, SSH_CLIENT_ERROR, "Chave de host alterada ou invalida.");
    return false;
}

void ssh_client_task(void *pvParameters)
{
    const ssh_client_generation_t generation = static_cast<ssh_client_generation_t>(reinterpret_cast<uintptr_t>(pvParameters));
    ESP_LOGI(TAG, "Iniciando task SSH para %s@%s:%d", s_current_config.user.c_str(), s_current_config.host.c_str(),
             s_current_config.port);

    set_state_locked(generation, SSH_CLIENT_CONNECTING, "Conectando ao servidor...");

    std::call_once(s_ssh_init_once, []() {
        ssh_init();
    });

    ssh_session session = ssh_new();
    if (session == nullptr) {
        ESP_LOGE(TAG, "Falha ao alocar sessão SSH");
        set_state_locked(generation, SSH_CLIENT_ERROR, "Erro interno ao alocar sessão SSH.");
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    long timeout_sec = 10;
    unsigned int port_val = static_cast<unsigned int>(s_current_config.port);
    bool process_config = false;
    const char *ssh_dir = CYBERDECK_SSH_DIR;
    const char *known_hosts = CYBERDECK_KNOWN_HOSTS_PATH;

    setenv("HOME", CYBERDECK_CONFIG_DIR, 1);

    ssh_options_set(session, SSH_OPTIONS_HOST, s_current_config.host.c_str());
    ssh_options_set(session, SSH_OPTIONS_USER, s_current_config.user.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port_val);
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout_sec);
    ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &process_config);
    ssh_options_set(session, SSH_OPTIONS_SSH_DIR, ssh_dir);
    ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, known_hosts);
    ssh_options_set(session, SSH_OPTIONS_GLOBAL_KNOWNHOSTS, known_hosts);

    const int address_family = ssh_address_family_for_host(s_current_config.host.c_str());
    int rc = ssh_options_set(session, SSH_OPTIONS_ADDRESS_FAMILY, &address_family);
    if (rc != SSH_OK) {
        std::string err_msg = "Falha ao configurar família de endereços SSH: ";
        err_msg += ssh_connect_error(session, rc);
        ESP_LOGE(TAG, "%s", err_msg.c_str());
        set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    rc = ssh_connect(session);
    if (rc != SSH_OK) {
        std::string err_msg = "Falha na conexão: ";
        err_msg += ssh_connect_error(session, rc);
        ESP_LOGE(TAG, "%s", err_msg.c_str());
        set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    if (s_stop_requested) {
        ssh_disconnect(session);
        ssh_free(session);
        set_state_locked(generation, SSH_CLIENT_DISCONNECTED, "Conexão cancelada pelo usuário.");
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    if (!verify_host_key(generation, session)) {
        ssh_disconnect(session);
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    // Tenta autenticação inicial sem senha
    rc = ssh_userauth_none(session, NULL);
    if (rc == SSH_AUTH_ERROR) {
        std::string err_msg = "Erro durante autenticação: ";
        err_msg += ssh_get_error(session);
        ESP_LOGE(TAG, "%s", err_msg.c_str());
        set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
        ssh_disconnect(session);
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    if (rc != SSH_AUTH_SUCCESS) {
        // Solicita senha ao usuário
        set_state_locked(generation, SSH_CLIENT_NEED_PASSWORD, "Aguardando senha...");

        char password_buf[128] = {0};
        bool got_password = false;

        while (!s_stop_requested) {
            if (xQueueReceive(s_password_queue, password_buf, pdMS_TO_TICKS(100)) == pdTRUE) {
                got_password = true;
                break;
            }
        }

        if (!got_password || s_stop_requested) {
            explicit_bzero(password_buf, sizeof(password_buf));
            ssh_disconnect(session);
            ssh_free(session);
            set_state_locked(generation, SSH_CLIENT_DISCONNECTED, "Autenticação cancelada.");
            s_task_handle.store(nullptr, std::memory_order_release);
            vTaskDeleteWithCaps(NULL);
            return;
        }

        set_state_locked(generation, SSH_CLIENT_AUTHENTICATING, "Autenticando...");
        rc = ssh_userauth_password(session, NULL, password_buf);
        // Limpa buffer de senha da memória por segurança
        explicit_bzero(password_buf, sizeof(password_buf));

        if (rc != SSH_AUTH_SUCCESS) {
            std::string err_msg = "Autenticação falhou: ";
            err_msg += ssh_get_error(session);
            ESP_LOGE(TAG, "%s", err_msg.c_str());
            set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
            ssh_disconnect(session);
            ssh_free(session);
            s_task_handle.store(nullptr, std::memory_order_release);
            vTaskDeleteWithCaps(NULL);
            return;
        }
    }

    ESP_LOGI(TAG, "Autenticado com sucesso. Abrindo canal PTY...");

    ssh_channel channel = ssh_channel_new(session);
    if (channel == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar canal SSH");
        set_state_locked(generation, SSH_CLIENT_ERROR, "Falha ao criar canal SSH.");
        ssh_disconnect(session);
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        std::string err_msg = "Falha ao abrir sessão no canal: ";
        err_msg += ssh_get_error(session);
        ESP_LOGE(TAG, "%s", err_msg.c_str());
        set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    rc = ssh_channel_request_pty_size(channel, "xterm-256color", 80, 24);
    if (rc != SSH_OK) {
        ESP_LOGW(TAG, "Falha ao requisitar PTY específico, tentando pty padrão");
        ssh_channel_request_pty(channel);
    }

    rc = ssh_channel_request_shell(channel);
    if (rc != SSH_OK) {
        std::string err_msg = "Falha ao solicitar shell interativo: ";
        err_msg += ssh_get_error(session);
        ESP_LOGE(TAG, "%s", err_msg.c_str());
        set_state_locked(generation, SSH_CLIENT_ERROR, err_msg.c_str());
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        s_task_handle.store(nullptr, std::memory_order_release);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    set_state_locked(generation, SSH_CLIENT_CONNECTED, "Conectado.");
    ESP_LOGI(TAG, "Sessão SSH interativa estabelecida.");

    char rx_buffer[1024];
    std::string input_buffer;

    // Loop de transmissão e recepção não-bloqueante de alta vazão
    while (!s_stop_requested && ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
        // 1. Processa dados de entrada do usuário a serem enviados
        char incoming_char = 0;
        while (xQueueReceive(s_input_queue, &incoming_char, 0) == pdTRUE) {
            input_buffer.push_back(incoming_char);
        }

        if (!input_buffer.empty()) {
            int written = ssh_channel_write(channel, input_buffer.data(), (uint32_t)input_buffer.size());
            if (written > 0) {
                input_buffer.erase(0, (size_t)written);
            } else if (written < 0) {
                ESP_LOGE(TAG, "Erro ao escrever no canal SSH: %s", ssh_get_error(session));
                break;
            }
        }

        // 2. Lê saída remota (stdout) drenando enquanto houver dados
        int nbytes = 0;
        bool had_data = false;
        do {
            nbytes = ssh_channel_read_nonblocking(channel, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (nbytes > 0) {
                rx_buffer[nbytes] = '\0';
                dispatch_rx(generation, rx_buffer, (size_t)nbytes);
                had_data = true;
            } else if (nbytes < 0) {
                ESP_LOGE(TAG, "Erro na leitura do canal SSH: %s", ssh_get_error(session));
                break;
            }
        } while (nbytes > 0);

        if (nbytes < 0) {
            break;
        }

        // 3. Lê saída de erro (stderr)
        do {
            nbytes = ssh_channel_read_nonblocking(channel, rx_buffer, sizeof(rx_buffer) - 1, 1);
            if (nbytes > 0) {
                rx_buffer[nbytes] = '\0';
                dispatch_rx(generation, rx_buffer, (size_t)nbytes);
                had_data = true;
            } else if (nbytes < 0) {
                ESP_LOGE(TAG, "Erro na leitura de stderr SSH: %s", ssh_get_error(session));
                break;
            }
        } while (nbytes > 0);

        if (nbytes < 0) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(had_data ? 5 : 15));
    }

    ESP_LOGI(TAG, "Encerrando sessão SSH...");
    set_state_locked(generation, SSH_CLIENT_DISCONNECTING, "Desconectando...");

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);

    ssh_disconnect(session);
    ssh_free(session);

    set_state_locked(generation, SSH_CLIENT_DISCONNECTED, "Conexão SSH encerrada.");
    s_task_handle.store(nullptr, std::memory_order_release);
    vTaskDeleteWithCaps(NULL);
}

} // namespace

esp_err_t ssh_client_connect(const char *user, const char *host, int port, ssh_rx_cb_t rx_cb, ssh_state_cb_t state_cb)
{
    if (user == nullptr || host == nullptr || strlen(host) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state_mutex == nullptr) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    if (s_input_queue == nullptr) {
        s_input_queue = xQueueCreate(2048, sizeof(char));
    }
    if (s_password_queue == nullptr) {
        s_password_queue = xQueueCreate(1, sizeof(char) * 128);
    }
    if (s_host_key_queue == nullptr) {
        s_host_key_queue = xQueueCreate(1, sizeof(uint8_t));
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

    if (s_task_handle.load(std::memory_order_acquire) != nullptr ||
        (s_state.load(std::memory_order_acquire) != SSH_CLIENT_DISCONNECTED &&
         s_state.load(std::memory_order_acquire) != SSH_CLIENT_ERROR)) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    const ssh_client_generation_t previous_generation = s_generation.load(std::memory_order_relaxed);
    if (previous_generation == UINT64_MAX) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    const ssh_client_generation_t generation = previous_generation + 1;
    s_generation.store(generation, std::memory_order_release);

    // Limpa filas remanescentes
    xQueueReset(s_input_queue);
    xQueueReset(s_password_queue);
    xQueueReset(s_host_key_queue);

    s_current_config.user = user;
    s_current_config.host = host;
    s_current_config.port = (port > 0) ? port : 22;

    s_rx_cb = rx_cb;
    s_state_cb = state_cb;
    s_stop_requested.store(false, std::memory_order_release);

    TaskHandle_t task = nullptr;
    BaseType_t ret = xTaskCreateWithCaps(ssh_client_task, "ssh_client", 24576,
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(generation)), 5, &task,
                                         MALLOC_CAP_SPIRAM);
    if (ret == pdPASS) {
        s_task_handle.store(task, std::memory_order_release);
    } else {
        s_task_handle.store(nullptr, std::memory_order_release);
        ESP_LOGE(TAG, "Falha ao criar a task SSH em PSRAM: ret=%d", (int)ret);
    }

    xSemaphoreGive(s_state_mutex);

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t ssh_client_accept_host_key(void)
{
    if (s_host_key_queue == nullptr || s_state.load(std::memory_order_acquire) != SSH_CLIENT_NEED_HOST_KEY) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t accepted = 1;
    return xQueueSend(s_host_key_queue, &accepted, pdMS_TO_TICKS(500)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ssh_client_send_password(const char *password)
{
    if (password == nullptr || s_password_queue == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[128] = {0};
    const size_t length = strnlen(password, sizeof(buf) - 1);
    memcpy(buf, password, length);
    buf[length] = '\0';
    BaseType_t ret = xQueueSend(s_password_queue, buf, pdMS_TO_TICKS(500));
    explicit_bzero(buf, sizeof(buf));
    if (ret != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ssh_client_send_data(const char *data, size_t len)
{
    if (data == nullptr || len == 0 || s_input_queue == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (xQueueSend(s_input_queue, &c, pdMS_TO_TICKS(50)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

void ssh_client_disconnect(void)
{
    s_stop_requested.store(true, std::memory_order_release);
}

bool ssh_client_disconnect_and_wait(uint32_t timeout_ms)
{
    ssh_client_disconnect();

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while (s_task_handle.load(std::memory_order_acquire) != nullptr) {
        if (timeout_ticks == 0 || (xTaskGetTickCount() - start) >= timeout_ticks) return false;
        vTaskDelay(1);
    }
    return true;
}

bool ssh_client_is_active(void)
{
    const ssh_client_state_t state = s_state.load(std::memory_order_acquire);
    return state != SSH_CLIENT_DISCONNECTED && state != SSH_CLIENT_ERROR;
}

ssh_client_state_t ssh_client_get_state(void)
{
    return s_state.load(std::memory_order_acquire);
}

ssh_client_generation_t ssh_client_generation(void)
{
    return s_generation.load(std::memory_order_acquire);
}
