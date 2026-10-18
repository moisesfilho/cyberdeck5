# Plano 09 — Notas de voz

## Objetivo

Gravar e reproduzir pequenos memorandos localmente usando microfones e codec do
Tab5, salvando no SD somente por solicitação do usuário.

## Hardware e dependências

Documentação do Tab5 e BSP indicam ES7210 com dois microfones e ES8388; APIs
`bsp_audio_codec_microphone_init()` e `esp_codec_dev_read()` estão documentadas.
Compartilhamento de I2S/codec com o player requer ownership único.

## Escopo incremental

1. Captura PCM curta, indicador visível, duração e nível.
2. Salvar WAV bounded usando transação segura de storage; solicitar nome e
   confirmação.
3. Reproduzir a gravação pelo player.
4. Compressão/transcrição são extensões futuras, após medir recursos e decidir
   processamento local versus envio consentido.

## Privacidade e segurança

Gravação exige ação explícita; indicador permanece visível até parar; sem
gravação em background, inicialização automática ou transmissão. Descartar
buffers após stop. Limitar duração/espaço, fechar arquivo corretamente e limpar
temporários em falha.

## Etapas e aceite

Testar microfone, níveis, saturação, silêncio, cartão ausente/cheio, interrupção,
falha de codec, WAV inválido e teardown. Confirmar no hardware que captura não
bloqueia UI, indicador é claro e speaker/jack/player não disputam I2S.
Transcrição não faz parte do aceite desta etapa.
