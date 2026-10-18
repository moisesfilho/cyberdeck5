# Plano 03 — Player de áudio local

## Objetivo

Implementar `cyberdeck.music` para arquivos do SD, com controles no TUI e
reprodução em segundo plano. O Tab5 documenta codec ES8388, alto-falante e jack
de 3,5 mm; o BSP no repositório contém inicialização de áudio, mas o cyberdeck5
ainda não tem player.

## Referências e arquitetura

DECK//OS para Tab5 demonstra MP3, fila por pasta, metadados, seek, volume e
reprodução entre módulos. Avaliar ESP-GMF/ESP-ADF e decoders disponíveis antes
de escolher dependências; não migrar o projeto inteiro de framework. Isolar
codec/decoder num serviço proprietário, fora da UI.

## Escopo incremental

1. MP3 do SD, seleção, play/pause, stop, próxima/anterior e volume; saída pelo
   speaker e jack conforme BSP.
2. Fila por diretório, ID3, seek, shuffle/repeat e progresso.
3. Indicador global de reprodução e retorno à app.

## Limites e etapas

Validar caminho dentro da raiz virtual do SD; recusar symlinks e arquivos acima
do limite. Decodificação/I/O em worker, buffers e fila bounded; stop/join antes
de liberar codec/SD. Volume inicial conservador, mute/stop imediatos e amplificador
desligado quando ocioso. Bluetooth A2DP não faz parte: o gerenciador atual é BLE
LE.

Auditar BSP/`esp_codec_dev` e ownership de I2S; testar exemplo mínimo; criar
serviço, modelo host-testável e app; testar formato inválido, EOF, cartão removido,
falha de codec, pausa/resume e concorrência; validar speaker, fone, ruído em
repouso e consumo no hardware.

## Aceite

Áudio não bloqueia UI; falhas não deixam amplificador ligado; parar encerra
playback bounded; arquivos nunca são alterados. Validar também lifecycle e
coexistência com outras apps.
