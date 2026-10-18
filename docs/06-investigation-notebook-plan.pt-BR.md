# Plano 06 — Caderno de investigação

## Objetivo

Reunir resultados selecionados de diagnóstico, notas e horário numa sessão local
revisável e exportável pelo usuário.

## Base existente

O projeto tem editor compilado, storage confinado do SDK, relógio e cartão SD;
event log e Serial-JTAG também expõem dados, mas não devem ser copiados
automaticamente para o caderno.

## Escopo

- Criar sessão com título/nota, hora local e eventos escolhidos pelo usuário.
- Anexar resultados tipados de netdiag, Wi-Fi survey e BLE inspector após prévia
  e seleção de campos.
- Formato versionado, bounded, legível e importável sem executar conteúdo.
- Exportação explícita, opção de mascarar SSID/BSSID/IP e exclusão pelo gerenciador
  ou editor de arquivos.

## Etapas e aceite

Definir schema/limites; criar modelo puro e serializador/parser versionado;
integrar storage por transação segura; criar UI; testar arquivo truncado/corrompido,
tamanho máximo, sanitização, cancelamento de save e compatibilidade de versão.

Aceite: gravação exige ação explícita; valores não confiáveis são escapados;
leitura não executa comandos; falha de persistência não informa sucesso; usuário
consegue revisar e apagar a sessão. Retenção é visível e exportação oferece
mascaramento.
