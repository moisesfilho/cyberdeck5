# Validação manual de `poweroff` — Tab5

Estado: implementação para teste; **corte físico ainda não validado**.
Não inferir desligamento pela tela preta nem pela perda da USB Serial-JTAG.

## Antes do flash

1. Faça backup dos dados importantes do SD. Use inicialmente arquivos descartáveis.
2. Registre a identificação do Tab5, alimentação/bateria, horário, versão ESP-IDF
   e o SHA-256 de `build/cyberdeck5.bin`. O worktree pode conter mudanças não
   commitadas: apenas o nome do commit não identifica este firmware.
3. Leia `serial-bridge-validation.pt-BR.md`. Confirme a porta (normalmente
   `/dev/ttyACM0`) e feche qualquer processo que esteja usando essa porta.
4. Com o ambiente ESP-IDF carregado, execute manualmente:

   ```bash
   idf.py -p /dev/ttyACM0 flash
   ```

5. Confirme boot sem panic e obtenha `sys.info` pela CLI. Não faça teste de corte
   se o boot estiver incompleto, se o SD não montar ou se existirem erros de I/O.

## Confirmação e cancelamento — ainda com USB conectada

No shell **local**, sem editor aberto e sem sessão SSH conectada:

- `help` deve incluir `poweroff`; `poweroff --help` não deve armar confirmação.
- Digite `poweroff` e Enter: deve aparecer o pedido de confirmação, sem corte.
- Teste separadamente `Y`, `yes`, espaço antes/depois de `y`, Enter vazio e Escape.
  Todos cancelam. Um `y` posterior ao cancelamento não deve desligar.
- Confira as mensagens com `term.dump`. Confirmações diferentes não são
  executadas como outros comandos.
- Não use `poweroff` em SSH: naquela sessão o texto pertence ao servidor remoto,
  não ao Tab5. Saia da sessão remota antes de testar o comando local.

## Persistência e corte físico

1. Crie um arquivo descartável em `/data`, com conteúdo conhecido, usando o
   editor ou a CLI `fs.write`. Salve, feche o editor e confirme o conteúdo por
   `cat`/`term.dump`. Registre também configurações de Wi-Fi/tela alteradas.
2. Aguarde o fim de saves/auditorias/reconexões. Uma gravação pendente sem
   confirmação ou um erro de persistência **deve impedir o corte**.
3. Para o primeiro corte físico, use bateria em boas condições e retire a USB
   e outras fontes externas, respeitando as instruções do fabricante. Use o
   teclado físico para enviar `poweroff`, Enter, `y`, Enter.
4. Aguarde até 45 s de preparação. A sequência para admissão de I/O, fecha
   serviços SD/NVS, drena leases e logger, escreve/sincroniza `.poweroff-sync`,
   desmonta o SD antes de retirar seu LDO e emite três pulsos de 100 ms high/
   100 ms low em expander B (`0x44`), P4 (`PWROFF_PLUSE`).
5. Confirme o desligamento físico real por comportamento de alimentação/rail,
   usando instrumentação adequada se disponível. Tela apagada não é prova.
6. Religue pelo procedimento normal do Tab5. Confirme boot, montagem do SD,
   conteúdo completo do arquivo, configurações e evento persistido de poweroff.
   Não deve haver truncamento, corrupção, loop de boot ou desaparecimento de dados.
7. Depois repita em condições de alimentação externa controladas. Registre
   separadamente o comportamento com USB, bateria e outras fontes; permanecer
   alimentado por USB não confirma nem refuta sozinho o corte do rail esperado.

## Falhas e timeout

- Teste shutdown durante leitura/export apenas com dados descartáveis. Se houver
  timeout ou export/config pendente não confirmado, o terminal deve informar
  falha/etapa; nenhum pulso é autorizado pelas fases de preparação.
- A tentativa é one-shot. Após falha, a UI e o gate permanecem bloqueados e os
  recursos potencialmente vivos são retidos. Não há retry/reabertura automática:
  registre a mensagem e faça reboot controlado antes de outra tentativa.
- A ponte Serial é parada durante a sequência; ausência de resposta da CLI ou
  desaparecimento da porta **não** comprova corte físico. Preserve os logs já
  recebidos e fotografe a mensagem no display quando a ponte não estiver disponível.
- Não retire o SD/alimentação durante gravação para simular falhas. Injeções de
  write/fsync/close, timeout e erro de serviço já têm testes host; falhas físicas
  devem ser induzidas somente com procedimento e instrumentação seguros.

## Evidências de aceite

Registre dispositivo, binário/hash, alimentação, arquivos/hash antes e depois,
comandos, mensagens, horários, logs de boot e resultado físico observado.
Somente depois desses resultados o plano pode marcar **HW-ACCEPT PASS**.
Se o dispositivo continuar ligado, o sinal enviado não deve ser tratado como
desligamento concluído; não há repetição automática do pulso.
