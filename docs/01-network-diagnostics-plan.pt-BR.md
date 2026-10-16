# Plano 01 — Diagnóstico de rede

## Objetivo

Criar `cyberdeck.netdiag` para diagnosticar conectividade de um host ou serviço
informado pelo usuário, usando o Wi-Fi conectado sem varredura ampla ou ações
contra terceiros.

## Estado e dependências atuais

- `wifi_mgr` mantém conexão, IP, scan e estado; o shell oferece `wifi` e
  `wifi audit`.
- Há cliente HTTP usado pela sincronização de horário; não há utilitário geral
  de ping, DNS ou teste TCP documentado como serviço do SDK.
- Aplicativos usam supervisor, grants e fachadas. Não chamar ESP-IDF diretamente
  da UI.

## Escopo

1. Mostrar interface, IP, gateway, DNS e RSSI disponíveis.
2. Executar sob demanda resolução DNS, ping/latência se suportado, conexão TCP
   para host+porta explícitos e requisição HTTP/TLS a URL informada.
3. Exibir etapa, duração, resultado e erro tipado sem bloquear LVGL.
4. Permitir repetição limitada e exportação opcional de resumo sanitizado.

## Não objetivos

Sem varredura de portas, exploração, captura de credenciais, teste em lote,
deauth, spoofing, interceptação ou alteração de tráfego. Confirmar suporte ICMP
antes de anunciar ping. HTTPS deve validar certificados.

## Etapas e aceite

Definir contrato host-testável para etapas, timeout, cancelamento e resultados;
confirmar APIs DNS/TCP/HTTP e compatibilidade com o C6; implementar worker/fila
bounded; registrar a app com grant de rede; criar testes de timeout, falha,
cancelamento, respostas stale e coexistência com SSH; validar no Tab5 conectado
e desconectado.

Aceite: nenhuma operação bloqueia LVGL; destino/porta são explícitos; frequência
de tentativas é limitada; DNS/TCP/TLS distinguem suas falhas; logs não contêm
credenciais, tokens ou conteúdo de tráfego; host tests, contratos, build e roteiro
Serial-JTAG passam.

## Riscos

Confirmar suporte ICMP e coexistência de testes ativos com Wi-Fi/SSH. Timeout,
cancelamento e certificado inválido devem falhar de forma explícita.

## Viabilidade e decisão aprovada

### Fatos técnicos verificados

- No ESP-IDF 5.5.5 local, `lwip_getaddrinfo` (`lwip/src/api/netdb.c:417-498`)
  usa `netconn_gethostbyname_addrtype` síncrono. A espera pelo resultado em
  `lwip/src/api/api_lib.c:1274-1347` não fornece timeout de chamada comprovado,
  e não há cancelamento por consulta.
- A API `dns_gethostbyname_addrtype` (`lwip/src/include/lwip/dns.h:96-114`)
  é assíncrona. Literal IP e cache podem concluir inline; cache miss retorna
  `ERR_INPROGRESS` e conclui pelo callback. `dns_check_entry`
  (`lwip/src/core/dns.c:1153-1219`) limita retries e chama callback com NULL ao
  expirar. Com `DNS_TMR_INTERVAL=1000 ms`, `DNS_MAX_RETRIES=4` e
  `DNS_MAX_SERVERS=3`, cerca de 21 s é apenas uma estimativa nominal condicional,
  não um limite wall-clock: atraso/starvation do timer ou `tcpip_thread` pode
  postergar o callback. A API não oferece cancelamento.
- Com `LWIP_TCPIP_CORE_LOCKING=0`, a consulta DNS deve ser iniciada por
  `tcpip_callback` no contexto `tcpip_thread`. O contexto do callback precisa
  permanecer válido. Um único slot estático bounded com geração pode descartar
  resultados stale, mas deve continuar reservado até o callback: se ele não
  ocorrer, novas consultas DNS ficam recusadas até callback ou reboot.
- O TCP existente usa socket nonblocking e `select` com deadline de 3000 ms,
  antes da execução HTTP. Qualquer assert sobre isso depende de confirmar o
  adapter usado e a propriedade do socket.
- `esp_http_client_config_t.is_async` requer chamadas sequenciais repetidas ao
  mesmo handle; `ESP_ERR_HTTP_EAGAIN` pede continuação. Chamadas simultâneas não
  são permitidas e cleanup só é seguro após `perform` concluir/sair. `timeout_ms`
  limita I/O/handshake por chamada, não um deadline global. `cancel_request()`
  fecha e reconecta, não sendo cancelamento seguro. Para TLS com URL resolvida
  para IP, `common_name` preserva hostname/SNI, mas o header HTTP `Host` também
  deve conter o hostname original. Redirects devem permanecer desativados e
  validação de certificado/CA bundle é obrigatória.
- Nenhuma dessas APIs garante limite wall-clock se task scheduling ou `tcpip`
  deixar de progredir.

### Opções e decisão humana

1. Manter DNS/TCP/HTTPS no escopo com cancelamento lógico imediato: a UI/worker
   retorna cancelado sem esperar a rede, resultados de operações canceladas ou
   gerações antigas são descartados. DNS usa um único contexto retido até callback,
   falha fechada e rejeita nova consulta enquanto o slot estiver ocupado; se o
   callback não vier, a recuperação pode exigir reboot. HTTPS usa polling async
   sequencial e só libera handle/recursos ao sair de `perform`.
2. Exigir hard wall-clock bound para toda operação: a abordagem lwIP/HTTP acima
   não comprova esse requisito. Isso exige avaliar e validar resolver/transporte
   próprios ou aprovar redução formal do escopo; não se deve substituir APIs nem
   eliminar etapas silenciosamente.

**Decisão humana aprovada: opção 1.** Manter DNS/TCP/HTTPS explícitos, com
cancelamento lógico imediato para a UI. O DNS usa um único contexto estático
bounded retido até callback; enquanto ocupado, rejeita nova consulta. O callback
ausente pode exigir reboot. Não prometer limite wall-clock sob starvation do
scheduler/tcpip. HTTPS deve ser async, com polling sequencial sem chamadas
 concorrentes ao handle; cleanup somente após `perform()` sair. O requisito
 original de conectar ao IP diagnosticado foi substituído pela decisão humana
 posterior: HTTPS usa o hostname no cliente público ESP-IDF e pode resolver um
 IP diferente dos testes DNS/TCP explícitos. SNI, validação TLS/CA e `Host`
 preservam o hostname; apenas HEAD, sem corpo enviado, redirects ou fallback GET.
 HTTP 405 é resultado final. Qualquer corpo de resposta é rejeitado; o risco de
 metadados de headers sem hard cap configurável no ESP-IDF 5.5.5 foi aceito.

Esta aprovação aceita expressamente as limitações e o risco alto, não substitui
os requisitos de segurança nem valida a implementação. Antes de declarar o
recurso implementado, a produção deve compartilhar uma costura real exercitável
pelos testes; o tester é responsável por criar testes e matriz REQ/AC/TEST. Não
usar DNS síncrono, não matar worker para simular cancelamento, nem liberar/reusar
o contexto DNS antes do callback. Semântica aprovada não autoriza varreduras,
ICMP/ping, exportação, tráfego implícito ou comandos automáticos.

### Progresso e gates pendentes (16/out/2026)

O controller e os adapters DNS/TCP/HTTPS estão integrados à app e ao runtime.
Os testes host executam os TUs reais com shims nas fronteiras de plataforma;
`test_netdiag_app` verifica 47 cenários/assertivas incluindo grant revogado,
cancelamento e quarentena do worker. Os alvos focados também geram cobertura.
O agregado e o build ESP-IDF passaram localmente; com GCC/gcov 13.4 local, os
gates gcovr passaram (86,5% linhas, 94,9% funções, 59,2% branches; mínimo de
branches 59%). O GCC/gcov 15.2 do host ainda mede 57,4% de branches com o
mesmo agregado: os resultados não são intercambiáveis entre toolchains.

O Quality Gate remoto passou no commit `dbe3a94` (run `38916411814`) e no
commit `8e41338` (segunda tentativa do run `38918012957`); a primeira tentativa
do segundo run falhou em uma asserção de `test_time_sync`, fora do escopo netdiag.
O firmware `8e41338` foi gravado em `/dev/ttyACM0` e validado via `term.dump`:
status, DNS, TCP e HTTPS 200 para `example.com`, recusa de URL HTTP, rate limit
e cancelamento HTTPS. `sys.info` confirmou versão, uptime contínuo e ausência
de safe mode. Trata-se de validação dirigida, não de captura de tráfego nem de
teste de HTTP 405/TLS negativo ou do roteiro serial completo. Qualquer commit
posterior exige CI no novo estado antes da conclusão e aceite humano.
