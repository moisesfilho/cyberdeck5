# Diagnóstico explícito de rede

Objetivo final aprovado: o app `cyberdeck.netdiag` aceitará somente comandos explícitos no shell:

- `netdiag status` consulta a interface STA e mostra IP, gateway, DNS e RSSI
  disponíveis; ausência é apresentada como `<missing>`.
- `netdiag dns <host>` resolve somente o host indicado.
- `netdiag tcp <host> <port>` tenta somente o par informado.
- `netdiag https <url>` envia somente HEAD para uma URL `https://`, usando o
  bundle de certificados do ESP-IDF. Redirects automáticos são desabilitados;
  405 é um status HTTP final, sem fallback GET.
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
não são aceitos pelo parser/interface de usuário; IPv4 continua aceito como host.
Os adapters internos aceitam endpoints numéricos IPv4/IPv6, incluindo respostas
IPv6 de DNS, sem ampliar o formato de entrada do usuário.

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

HTTPS usa `esp_http_client` async, polled sequencialmente, sem chamada concorrente
ao handle. Redirects são desabilitados por `disable_auto_redirect=true` (não por
`max_redirection_count=0`, que pode ser interpretado como default pelo IDF).
Cleanup só ocorre após `perform()` retornar; cancelamento é lógico e
cleanup acontece em polling posterior, entre chamadas. Conecta ao IP resolvido,
preserva hostname original em SNI/verificação e `Host` (incluindo porta não
default), usa CA bundle e não desabilita validação. Executa HEAD, sem GET fallback
em 405 e sem corpo de aplicação. O status HTTP e duração são os dados bounded
retidos; não se afirma limite total de resposta/cabeçalhos: ESP-IDF 5.5.5 acumula
metadados de response headers sem hard cap configurável nesta integração, risco
aceito explicitamente pelo usuário.

TCP recebe endpoint IP numérico e porta explícita, usa socket não bloqueante,
select de 3000 ms e consulta SO_ERROR. O worker é singleton; cancelamento lógico
não interrompe a chamada de rede. `poll` permanece pending enquanto o worker
executa; após fechar o socket exatamente uma vez, o worker libera autonomamente
o slot cancelado e o token passa a `unknown_token`, sem polling posterior.
Cancelamento não publica resultado. Starvation do scheduler impede promessa de
limite wall-clock.

**Integração local implementada; revisão final ainda pendente.** O app compilado
`cyberdeck.netdiag` é registrado como foreground command app e só inicia por um
comando explícito `netdiag`; registrar o manifest não inicia worker nem tráfego.
Depende de `cyberdeck.event_log` e `cyberdeck.wifi`, recebe somente grant `network`
e revalida o grant no worker antes de cada etapa, poll e publicação. Um worker
proprietário serializa core e adapters; comandos de rede retornam sem esperar o
resultado e `netdiag result` consulta o snapshot sanitizado. Um item ativo e fila
de uma entrada impedem operação simultânea; a janela de admissão é de 1000 ms.
Cancelamento é lógico. TCP mantém o slot até o worker fechar o socket; HTTPS
limpa o handle somente após `perform()` retornar; DNS pode reter seu slot até
callback. Stop usa join cooperativo bounded e, em timeout, conserva app/recursos
em estado fail-closed sem destruir contexto usado pelo worker.

`netdiag status` consulta explicitamente estado STA, IP, gateway, DNS e RSSI
disponíveis; dados indisponíveis aparecem como `<missing>`. Os resultados não
retêm nem exibem destino, hostname, IP resolvido, URL, headers ou payload. O
worker TCP usa stack de 8192 bytes, aumentada conservadoramente de 4096 por causa
da cadeia socket/select/SO_ERROR; build não substitui medição de high-water mark
nem validação física. O risco aceito de metadados acumulados de response headers
sem hard cap no ESP-IDF 5.5.5 continua aplicável.

TEST-ND-07/08 e revisão local/CI final ainda precisam validar o app real e a
integração. Esta implementação não foi flashada; não declarar o comportamento de
hardware validado nem liberar enquanto o CI remoto do delta TCP/HTTPS não estiver
verde no estado exato.

O protótipo removido não satisfazia a decisão: sua resolução usava `getaddrinfo`
síncrono, e HTTPS executava `perform()` uma única vez em modo síncrono. Não recriar
nem reativar esse fluxo. Esta etapa não completa requisitos de rede e não deve ser
flashada/liberada. O tester deve validar os adapters reais conforme TEST-ND-04..06.
Não é evidência de validação Serial-JTAG. Sem comandos automáticos,
scans, ICMP/ping, exportação ou tráfego implícito.
