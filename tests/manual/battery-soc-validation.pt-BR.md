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

Esperado: `fw_version` igual a versao gravada (hoje `1.5.0`), heap estavel, sem
panic/reboot. Registrar uptime inicial.

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

## 3. SoC estavel no plato (variacao de no maximo 1 ponto)

Com o cabo conectado e a bateria proxima do corte:

1. Repetir `ui.type "battery protection status"` a cada ~10 s por 2 min.
2. Observar `percentage` e o percentual do header (`ui.dump` ou `screen.dump`).

Esperado:

- o percentual nao oscila entre bins distantes; a variacao e de no maximo 1 ponto
  por amostra e compativel com a corrente integrada;
- carregando em ~8190 mV / ~-250 mA o percentual fica no plato da politica
  (100 %, seed por OCV saturado), nao no valor antigo de 98 % da tensao terminal;
- em repouso no plato o header fica em {99, 100} e varia no maximo 1 ponto em
  10 min; pode alternar 100 <-> 99 com o truncamento (100 % = carga maxima desta
  politica, nao pack pleno), mas nunca volta a bins distantes como 94 <-> 98.

## 4. Corte e latch

1. Deixar carregar ate o corte (`protection=active`, `charger=off`).
2. Nao desconectar o cabo e repetir o status por 5 min.

Esperado:

- exatamente **uma** transicao de `charger` (on -> off) no corte;
- nenhuma religacao enquanto o SoC estimado estiver acima de 90 e houver fonte
  externa; estados `external`, `unknown` ou leitura INA invalida nao liberam o
  latch;
- nenhuma oscilacao de CHG_EN (monitor serial nao deve mostrar novos
  `Charger enable write`).

## 5. Liberacao do latch

- Com o cabo desconectado, aguardar descarga ate o SoC estimado chegar a <= 90
  (90 % da janela de 2100 mV fica em ~7890 mV, a mesma banda fisica de antes):
  esperado `charger=on` imediatamente na primeira amostra (sem votos).
- Alternativa (sem esperar descarga): manter sem fonte externa por 5 amostras
  consecutivas (`battery`/`absent`) e confirmar a liberacao na quinta.
- `battery protection off` deve liberar na hora (`active=0`, `charger=on`);
  `battery protection on` sem fonte externa **nao** deve recortar.

## 6. Reset por ausencia

Remover a bateria (com o dispositivo em bancada e seguro):

- `state=absent`, `available=true`, glyph externo **sem** percentual no header;
- `percentage=0` no status (estimador reiniciado, sem valor fabricado);
- recolocar a bateria: novo seed por OCV (salto direto ao valor semeado,
  sem convergencia ponto a ponto a partir do valor anterior).

## 7. Estabilidade final

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
```

Esperado: uptime continuo (sem reboot), heap sem degradacao evidente e nenhum
`Guru Meditation` no monitor durante todo o roteiro.

## Criterios de aceite

- escala de corrente consistente (300 uA/LSB) no status e no diagnostico;
- percentual do header = SoC estimado, sem oscilacao entre bins distantes no
  corte; no plato a variacao fica em no maximo 1 ponto (100 <-> 99 por
  truncamento);
- exatamente uma transicao de CHG_EN no corte e latch estavel;
- liberacao apenas por SoC <= 90, 5 amostras sem fonte ou disable;
- ausencia reinicia o estimador sem percentual fabricado;
- sem panic, reboot ou perda de heap durante o roteiro.

Registrar: hash/versao do firmware, porta, comandos, respostas JSON, trechos do
monitor serial e horario de cada cenario.
