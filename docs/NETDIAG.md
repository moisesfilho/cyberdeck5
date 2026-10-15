# Diagnóstico explícito de rede

Objetivo final aprovado: o app `cyberdeck.netdiag` aceitará somente comandos explícitos no shell:

- `netdiag status` consulta a interface STA e mostra IP, gateway, DNS e RSSI
  disponíveis; ausência é apresentada como `<missing>`.
- `netdiag dns <host>` resolve somente o host indicado.
- `netdiag tcp <host> <port>` tenta somente o par informado.
- `netdiag https <url>` faz um GET para uma URL `https://`, usando o bundle de
  certificados do ESP-IDF. Redirects automáticos são desabilitados e o corpo é
  limitado a 1024 bytes e descartado.
- `netdiag result` consulta o último resultado tipado; `netdiag cancel` solicita
  cancelamento cooperativo.

Há um item ativo, alvo limitado a 255 bytes e intervalo mínimo de um segundo
entre inícios. A app depende dos serviços `cyberdeck.event_log` e
`cyberdeck.wifi` e retém somente o grant `network`. Sem ping/ICMP, scan,
fallback HTTP, exportação, redirects, enumeração, conteúdo de resposta ou
logging de destino/credenciais.

Hosts DNS/TCP e a autoridade da URL HTTPS aceitam nomes ASCII com labels de
1–63 caracteres alfanuméricos ou hífen, sem hífen no início/fim; o hostname
tem no máximo 253 caracteres. O ponto final (FQDN absoluto) e literais IPv6
não são aceitos pelo parser atual; IPv4 continua aceito como host.

## Requisitos/aceite canônicos

Ver `01-network-diagnostics-plan.pt-BR.md`: REQ-ND-01..06 e AC-ND-01..05 são os
IDs normativos. TEST-ND-01..02 cobrem parser/core; TEST-ND-03..08 aguardam adapters
e integração.

## Decisão técnica e estado de implementação

A decisão humana aprovou manter DNS/TCP/HTTPS explícitos com risco alto e
cancelamento lógico. DNS é iniciado no contexto `tcpip_thread`. Literal/cache hit
retorna `ERR_OK` com `ip_addr_t` imediatamente; cache miss retorna `ERR_INPROGRESS`
e conclui pela callback assíncrona no `tcpip_thread`. No caminho assíncrono, o slot
permanece ocupado até essa callback, mesmo após cancelamento. Uma nova consulta DNS
é recusada enquanto ocupado; ausência do
callback pode exigir reboot. Não há promessa de hard wall-clock sob starvation
do scheduler/tcpip.

HTTPS deve usar `esp_http_client` assíncrono, polled sequencialmente, nunca com
chamadas concorrentes no handle. Cleanup só depois de `perform()` sair. TLS/CA e
hostname devem ser validados preservando SNI e `Host` originais ao conectar ao IP
resolvido. Redirects ficam desligados e corpo limitado e descartado.

**Status intermediário: recurso inativo e indisponível.** O app síncrono WIP e seu
header foram removidos; não há fonte de app com caminho de execução. Core e adapter
DNS (`cyberdeck_netdiag_core.{h,cpp}`, `cyberdeck_netdiag_dns.{h,cpp}`) são compilados,
mas não registrados nem chamados pelo boot/app/shell. O adapter faz DNS assíncrono
somente após chamada explícita, via `tcpip_callback`. O resultado `ERR_OK` usa o
endereço de saída; somente `ERR_INPROGRESS` aguarda a callback lwIP. Slot singleton
é retido até callback ou descarte do trampoline cancelado. O caminho de callback
inline é apenas defensivo e não esperado no lwIP configurado. Cancelamento não libera cedo; a ausência
do callback pode manter o slot ocupado até reboot. Não promete limite wall-clock sob
starvation de `tcpip_thread`. Ver plano para estados e contrato detalhados.

O protótipo removido não satisfazia a decisão: sua resolução usava `getaddrinfo`
síncrono, e HTTPS executava `perform()` uma única vez em modo síncrono. Não recriar
nem reativar esse fluxo. Esta etapa não
completa requisitos de rede e não deve ser flashada/liberada. O tester deve validar
o core de produção contra a matriz REQ/AC/TEST. Não é evidência de validação Serial-JTAG. Sem comandos automáticos,
scans, ICMP/ping, exportação ou tráfego implícito.
