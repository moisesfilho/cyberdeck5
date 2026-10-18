# Plano 05 — Assistente de IA na LAN

## Objetivo

Oferecer chat textual com servidor de inferência na rede local (primeiro alvo:
Ollama), mantendo inferência e credenciais fora do ESP32-P4.

## Referência e limites

DECK//OS para Tab5 demonstra chat Ollama com streaming. Seu exemplo documenta
HTTP sem autenticação, apropriado somente a redes confiáveis. O cyberdeck5 deve
tratar endpoint, autenticação e dados enviados como decisões explícitas. Não se
prevê LLM local no dispositivo.

## Escopo

- Configurar host/porta/modelo por perfil; validar URL e limitar prompt, resposta,
  contexto e tempo.
- Chat streaming com cancelamento, limpeza e erros recuperáveis.
- Futuramente anexar resumo de netdiag, Wi-Fi survey ou BLE inspector somente
  após prévia e confirmação do conteúdo.
- Credenciais nunca aparecem em logs ou exportações de sessão.

## Não objetivos

Não executar comandos sugeridos pela IA, alterar rede, enviar dados em background
ou apresentar saída do modelo como diagnóstico autoritativo.

## Etapas e aceite

Definir protocolo Ollama, autenticação e TLS; implementar cliente worker com fila
bounded, streaming, cancelamento e backpressure; criar UI com stop/clear/modelo e
destino visível; testar respostas parciais, erros HTTP, payload inválido/oversized,
timeout, desconexão e segredos em logs; validar com servidor local.

Aceite: consentimento antes de compartilhar relatório; endpoint exibido; nenhum
comando executado; logs redigidos e streaming bounded. TLS não será anunciado até
certificados serem validados na configuração real.
