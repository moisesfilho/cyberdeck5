# Validacao manual - SoC de bateria e corte de carga por tensao terminal

Roteiro para validar no M5Stack Tab5 (ESP32-P4) a entrega do SoC estimado por
contagem de carga ancorada em OCV, a escala de corrente de 300 uA/LSB e o latch
de corte em 8200 mV. Executar **apos** flash do firmware compilado desta
entrega; registrar `sys.info` (versao e uptime) antes e depois de cada cenario.

Pre-condicoes:

- dispositivo conectado (`/dev/ttyACM0`, ajustar se necessario);
- `python3 -c 'import serial'` funciona e `python3 tools/cyberdeck_cli.py --help`
  responde;
- monitor serial aberto em paralelo para capturar boot/panic/reboot;
- fonte com limite de corrente e bateria em bancada controlada.

## 1. Identidade e boot

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
```

Esperado: `fw_version` desta entrega (1.3.0), heap estavel, sem panic/reboot.
Registrar uptime inicial.

## 2. Escala de corrente (300 uA/LSB)

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.type "battery protection status"
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 term.dump
```

Esperado:

- `current_ma` e `ina_current_ma` **iguais** (mesma conversao no reader);
- com o pack carregando, corrente na faixa de ~-250 mA (nao mais ~-370 mA da
  escala antiga de 1,525 mA/LSB);
- formato da linha inalterado (`state=`, `charge=`, `available=`, `voltage_mv=`,
  `current_ma=`, `percentage=`, `protection=`, `charger=`).

## 3. SoC sem oscilacao 94 <-> 98

Com o cabo conectado e a bateria proxima do corte:

1. Repetir `ui.type "battery protection status"` a cada ~10 s por 2 min.
2. Observar `percentage` e o percentual do header (`ui.dump` ou `screen.dump`).

Esperado:

- o percentual nao salta entre 94 e 98; a variacao e de no maximo 1 ponto por
  amostra e compativel com a corrente integrada;
- carregando em ~8190 mV / ~-250 mA o percentual fica no mesmo patamar do repouso
  (seed por OCV), nao em 98.

## 4. Corte e latch

1. Deixar carregar ate o corte (`protection=active`, `charger=off`).
2. Nao desconectar o cabo e repetir o status por 5 min.

Esperado:

- exatamente **uma** transicao de `charger` (on -> off) no corte;
- nenhuma religacao enquanto o SoC estimado estiver acima de 85 e houver fonte
  externa; estados `external`, `unknown` ou leitura INA invalida nao liberam o
  latch;
- nenhuma oscilacao de CHG_EN (monitor serial nao deve mostrar novos
  `Charger enable write`).

## 5. Liberacao do latch

- Com o cabo desconectado, aguardar descarga ate o SoC estimado chegar a <= 85:
  esperado `charger=on` imediatamente na primeira amostra (sem votos).
- Alternativa (sem esperar descarga): manter sem fonte externa por 5 amostras
  consecutivas (`battery`/`absent`) e confirmar a liberacao na quinta.
- `battery protection off` deve liberar na hora (`active=0`, `charger=on`);
  `battery protection on` sem fonte externa **nao** deve recortar.

## 6. Reset por ausencia

Remover a bateria (com o dispositivo em bancada e seguro):

- `state=absent`, `available=true`, glyph externo **sem** percentual no header;
- `percentage=0` no status (estimador reiniciado, sem valor fabricado);
- recolocar a bateria: novo seed por OCV (nao slew de 93/94).

## 7. Estabilidade final

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
```

Esperado: uptime continuo (sem reboot), heap sem degradacao evidente e nenhum
`Guru Meditation` no monitor durante todo o roteiro.

## Criterios de aceite

- escala de corrente consistente (300 uA/LSB) no status e no diagnostico;
- percentual do header = SoC estimado, sem oscilacao no corte;
- exatamente uma transicao de CHG_EN no corte e latch estavel;
- liberacao apenas por SoC <= 85, 5 amostras sem fonte ou disable;
- ausencia reinicia o estimador sem percentual fabricado;
- sem panic, reboot ou perda de heap durante o roteiro.

Registrar: hash/versao do firmware, porta, comandos, respostas JSON, trechos do
monitor serial e horario de cada cenario.
