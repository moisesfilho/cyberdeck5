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

## Requisitos e critérios canônicos

- **REQ-ND-01**: comandos/destinos explícitos (status, DNS de host, TCP host+porta,
  HTTPS URL) e exclusão de scan, ICMP/ping, exportação, tráfego implícito e terceiros.
- **REQ-ND-02**: core bounded host-linkable; worker/backend separado, estados e erros
  tipados; a UI não executa trabalho de rede.
- **REQ-ND-03**: DNS assíncrono em slot singleton; cancelamento lógico, slot retido até
  callback e quarentena enquanto o callback não chegar.
- **REQ-ND-04**: conexão TCP somente ao host/porta admitidos, timeout explícito e
  cancelamento lógico sem destruir recurso ainda usado.
- **REQ-ND-05**: HTTPS HEAD assíncrono sequencial, CA/hostname/SNI/Host válidos,
  redirects desligados e sem fallback GET (405 é status HTTP final). Não há corpo
  de aplicação; ESP-IDF 5.5.5 acumula metadados de response headers sem hard cap
  configurável, risco residual explicitamente aceito.
- **REQ-ND-06**: grants/lifecycle/stale-result/rate-limit e privacidade fail-closed,
  sem regressões em SSH e nos demais serviços.
- **AC-ND-01..05**: respectivamente UI não bloqueante; somente destino explícito;
  exclusividade/cancelamento/quarentena DNS; resultados TCP/HTTPS tipados e TLS válido;
  lifecycle/grants/limites/privacidade/regressões e gates aprovados.

IDs acima são a fonte canônica para rastreabilidade. Planejamento de testes: TEST-ND-01
(parser bounded), TEST-ND-02 (core: admissão, estado, cancelamento, stale, single-flight
e rate-limit), TEST-ND-03..08 (adapters DNS, TCP, HTTPS/TLS, lifecycle/grants, regressões
e integração/build/dispositivo), ainda pendentes conforme a fase.

## Entrega faseada e contrato do core

Estado atual: `cyberdeck_netdiag_core.{h,cpp}` e adapters DNS/TCP/HTTPS estão
implementados; o app foreground está registrado e compõe worker, grant e adapters.
O app não inicia no boot: somente acionamento explícito pode iniciar sua lifecycle
e produzir tráfego. Não há validação física nem aprovação final da integração.
O core é host-linkable e limita parsing, geração, single-flight, rate-limit e resultados
tipados. `backend.submit` é uma admissão de trabalho: se retornar falso, o resultado
terminal é `backend_unavailable`, `in_flight` é liberado e o intervalo não é consumido.
Timestamp zero é válido na primeira admissão; regressão monotônica falha com
`clock_regressed` sem alterar estado ou timestamp. Comandos de controle (`status`,
`result`, `cancel`) não podem ser admitidos como trabalho.

O adapter DNS usa um único slot global estático bounded e `tcpip_callback` para iniciar
`dns_gethostbyname_addrtype` no `tcpip_thread` (a configuração efetiva tem
`LWIP_TCPIP_CORE_LOCKING=0`). Trata `ERR_OK` síncrono, `ERR_INPROGRESS` e erros; o
callback também pode concluir inline. Publicação é protegida por seção crítica curta,
sem LVGL, logging, alocação ou mutex de task no callback. `cancel(token)` é lógico;
o slot não é liberado/reutilizado antes do callback real, ou da execução do trampoline
enfileirado que detecta cancelamento pré-resolução. `poll(token)` consome resultado e
reapta o slot; cancelamento não publica resultado. Até a prova de callback, nova
consulta retorna busy. Ausência de progresso pode reter o slot até reboot; não há
garantia wall-clock. O contrato lwIP pressupõe callback exatamente uma vez para
consulta aceita; callback duplicado após consumo não é detectável com segurança após
reuso do slot.

O protótipo síncrono anterior foi removido. A integração de `cyberdeck.netdiag`
registra o app foreground sob demanda, worker proprietário, grant de rede,
comandos explícitos e adapters DNS/TCP/HTTPS. Registro não inicia a app nem gera
tráfego; apenas o comando explícito inicia sua lifecycle. A integração ainda não
tem evidência comportamental que execute conjuntamente o app real, runtime/grants
e adapters reais; TEST-ND-07 permanece pendente e TEST-ND-08 é apenas estrutural.

### Progresso e evidências (15/out/2026)

- **DNS / TEST-ND-01..03:** core e adapter DNS foram implementados, revisados e
  testados; commit `8ff2748cb170288000605de66181297748cac29f` (DNS) tem Quality
  Gate remoto verde, run `38823591023`; cobertura local/host adapters reportada
  conforme a evidência registrada para essa etapa. Sem validação física no Tab5.
- **TCP, HTTPS e app / TEST-ND-04..06:** snapshot de integração commitado e
  publicado na branch `feature/netdiag-dns-stage`, commit
  `f7011f5f119ae1d2ee9f6d976b5918de68199883`. O Quality Gate desse commit,
  run `38833874806`, falhou no step 6 (Host aggregate and coverage):
  `coverage_scope_guard` não encontrou `cyberdeck_netdiag_app.cpp` no JSON gcovr.
  Etapas remotas subsequentes foram skipped. Embora revisão tenha reportado
  verdes as verificações locais (suite, build, lint e outras), isso não é
  aprovação: a revisão está `review_incomplete` e o gate remoto falhou.
  Um achado alto no app: `cancel_requested` permanece sticky após o primeiro
  `netdiag cancel`, fazendo consultas DNS/TCP/HTTPS posteriores também serem
  canceladas até stop/start. Corrigir e acrescentar regressão antes de aprovar.
- **App / TEST-ND-07..08:** o app está registrado como comando foreground, não
  inicia automaticamente no boot; ainda assim ficará exposto no firmware futuro,
  portanto não fazer flash nem release antes das correções e gates. TEST-ND-07,
  teste comportamental conjunto do app real, runtime/grants e adapters reais,
  continua ausente. TEST-ND-08 tem 20 checks estruturais e atualmente não entra
  no aggregate/CI; não equivale a teste comportamental nem a aprovação. O scope
  guard deve passar com evidência de cobertura verdadeira, não com allowlist
  cosmética. HTTPS permanece HEAD-only, sem fallback GET; 405 é resultado HTTP
  final tipado. Mantêm-se o risco aceito de metadados de headers sem hard cap no
  ESP-IDF 5.5.5, a exigência de confirmar no alvo a stack TCP de 8192 bytes e a
  limitação de fidelidade das critical sections no TSan host.
- **Gates pendentes:** criar seam/harness comportamental real para TEST-ND-07;
  corrigir o cancelamento sticky e cobri-lo por regressão; incluir TEST-ND-08 no
  aggregate/CI e incluir formatter/cppcheck do TU do app; obter cobertura real e
  passar o scope guard; executar build/revisão completa e Quality Gate remoto
  verde no estado final exato. Depois, validar via Serial-JTAG, incluindo stack
  high-water TCP de 8192 bytes, e obter aceite humano. Nenhum flash ou teste no
  dispositivo foi feito.
- **Riscos mantidos:** operação explícita, sem tráfego implícito; cancelamento
  lógico. DNS sem callback pode reter o slot até reboot. O risco de headers sem
  hard cap permanece aceito. Nenhuma evidência acima autoriza declarar o recurso
  concluído ou liberado.

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
  retorna `ERR_OK` e preenche `ip_addr_t` diretamente para literal IP/cache hit;
  cache miss retorna `ERR_INPROGRESS` e conclui pelo callback assíncrono.
  `dns_check_entry`
  (`lwip/src/core/dns.c:1153-1219`) limita retries e chama callback com NULL ao
  expirar. Com `DNS_TMR_INTERVAL=1000 ms`, `DNS_MAX_RETRIES=4` e
  `DNS_MAX_SERVERS=3`, cerca de 21 s é apenas uma estimativa nominal condicional,
  não um limite wall-clock: atraso/starvation do timer ou `tcpip_thread` pode
  postergar o callback. A API não oferece cancelamento.
- Com `LWIP_TCPIP_CORE_LOCKING=0`, a consulta DNS deve ser iniciada por
  `tcpip_callback` no contexto `tcpip_thread`. A callback lwIP assíncrona é
  executada nesse contexto; o caminho inline do adapter é apenas defensivo, não
  comportamento esperado do lwIP configurado. O contexto da callback precisa
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
   gerações antigas são descartados. DNS usa o endereço retornado em `ERR_OK`, ou
   retém um único contexto até callback quando recebe `ERR_INPROGRESS`; falha
   fechada e rejeita nova consulta enquanto ocupado; se o
   callback não vier, a recuperação pode exigir reboot. HTTPS usa polling async
   sequencial e só libera handle/recursos ao sair de `perform`.
2. Exigir hard wall-clock bound para toda operação: a abordagem lwIP/HTTP acima
   não comprova esse requisito. Isso exige avaliar e validar resolver/transporte
   próprios ou aprovar redução formal do escopo; não se deve substituir APIs nem
   eliminar etapas silenciosamente.

**Decisão humana aprovada: opção 1.** Manter DNS/TCP/HTTPS explícitos, com
cancelamento lógico imediato para a UI. O DNS usa um único contexto estático
 bounded durante a chamada e, somente em `ERR_INPROGRESS`, retido até callback;
 `ERR_OK` entrega o endereço diretamente. Enquanto ocupado, rejeita nova consulta.
 O callback ausente pode exigir reboot. Não prometer limite wall-clock sob starvation do
scheduler/tcpip. HTTPS deve ser async, com polling sequencial sem chamadas
concorrentes ao handle; cleanup somente após `perform()` sair. Conectar ao IP
resolvido sem perder SNI/validação TLS do hostname original nem o header `Host`;
  certificado/hostname validado, redirects desligados e HEAD sem fallback GET.
  Não há corpo de aplicação. O risco de metadados de headers acumulados sem hard
  cap no ESP-IDF 5.5.5 foi aceito; não declarar limite total de response.

Esta aprovação aceita expressamente as limitações e o risco alto, não substitui
os requisitos de segurança nem valida a implementação. Antes de declarar o
recurso implementado, a produção deve compartilhar uma costura real exercitável
pelos testes; o tester é responsável por criar testes e matriz REQ/AC/TEST. Não
usar DNS síncrono, não matar worker para simular cancelamento, nem liberar/reusar
o contexto DNS antes do callback. Semântica aprovada não autoriza varreduras,
ICMP/ping, exportação, tráfego implícito ou comandos automáticos.
