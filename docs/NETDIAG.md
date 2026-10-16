# Diagnóstico explícito de rede

O app compilado `cyberdeck.netdiag` aceita somente comandos explícitos no shell:

- `netdiag status` consulta a interface STA e mostra IP, gateway, DNS e RSSI
  disponíveis; ausência é apresentada como `<missing>`.
- `netdiag dns <host>` resolve somente o host indicado.
- `netdiag tcp <host> <port>` tenta somente o par informado.
- `netdiag https <url>` faz somente HEAD para uma URL `https://`, usando o bundle
  de certificados do ESP-IDF. Redirects automáticos são desabilitados; qualquer
  corpo recebido causa falha, sem fallback para GET. HTTP 405 é resultado final.
- `netdiag result` consulta o último resultado tipado; `netdiag cancel` solicita
  cancelamento cooperativo.

Há um item ativo, host limitado a 253 bytes, URL a 512 bytes e intervalo mínimo
de 30 segundos entre inícios. A app depende dos serviços `cyberdeck.event_log` e
`cyberdeck.wifi` e retém somente o grant `network`. Sem ping/ICMP, scan,
fallback HTTP, exportação, redirects, enumeração, conteúdo de resposta ou
logging de destino/credenciais.

## Decisão técnica e estado de implementação

A decisão humana aprovou manter DNS/TCP/HTTPS explícitos com risco alto e
cancelamento lógico. DNS deve usar a API assíncrona lwIP no contexto
`tcpip_thread`; um único slot permanece ocupado até callback, mesmo após
cancelamento. Uma nova consulta DNS é recusada enquanto ocupado; ausência do
callback pode exigir reboot. Não há promessa de hard wall-clock sob starvation
do scheduler/tcpip.

HTTPS usa `esp_http_client` assíncrono por hostname, com chamadas sequenciais
ao mesmo handle e cleanup somente depois de `perform()` sair. O cliente valida
TLS/CA e hostname (SNI), usando o `Host` original; pode conectar a um IP
diferente do medido nos testes DNS/TCP explícitos, conforme decisão humana.
Redirects ficam desligados e qualquer corpo recebido é rejeitado. O ESP-IDF
5.5.5 não oferece um hard cap configurável para metadados dos headers de
resposta; esse risco foi aceito, sem promessa de limite de memória.

O controller, os adapters e a app estão integrados e cobertos por testes host
com shims nas fronteiras externas. Isso não prova conectividade, TLS ou
comportamento no Tab5. O agregado local com GCC/gcov 13.4 passou os gates
(86,5% linhas, 94,9% funções, 59,3% branches); o Quality Gate do código no
commit `dbe3a94` passou (run `38916411814`). A validação Serial-JTAG ainda
está pendente; qualquer commit posterior requer CI no novo estado. O GCC/gcov
15.2 local mede branches diferentemente (57,4% no mesmo agregado).
Sem comandos automáticos, scans,
ICMP/ping, exportação ou tráfego implícito.
