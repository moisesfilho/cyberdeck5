# Validacao manual do GET /battery (somente leitura, LAN)

Roteiro para validar a telemetria de bateria pela rede, sem o cabo USB
Serial-JTAG. O endpoint nao amostra o INA226, nao abre I2C, nao adquire o lock do
display e nao escreve no SD: ele apenas projeta o snapshot de politica em cache
e o timestamp da ultima amostra.

Firmware esperado: `1.4.0` (`fw_version` no corpo da resposta deve conferir).

## Pre-requisitos

- Tab5 ligado no Wi-Fi, com IP obtido (o servidor HTTP sobe apenas com
  `enabled && has_ip` e cai quando o IP e perdido).
- Host de teste na mesma LAN privada do dispositivo. A rota usa a mesma
  allowlist do `/screenshot` (loopback, 10/8, 172.16/12, 192.168/16,
  169.254/16, ULA e IPv6 link-local/locais); um cliente fora dessas faixas
  recebe 403.
- Nenhum cabo USB conectado durante os cenarios (validacao pela LAN).

## Como descobrir o IP

No terminal do dispositivo, `wifi` mostra o estado detalhado. Se a ponte serial
estiver disponivel em outro momento, `sys.info` tambem informa o IP; para este
roteiro o esperado e nao precisar do USB.

## Comando base

```bash
curl -s http://<ip>/battery
```

Resposta esperada (uma linha, ordem fixa, sem quebras):

```json
{"schema":1,"state":"battery","charge":"not_charging","available":true,"bus_voltage_mv":7900,"current_ma":120,"percentage":88,"protection_enabled":true,"protection_active":false,"charger_enabled":true,"sampled_at_us":123456789,"sampled_age_ms":250,"fw_version":"1.4.0","uptime_s":42}
```

## Validacao dos campos

| Campo | O que conferir |
| --- | --- |
| `schema` | sempre `1` |
| `state` | `battery`, `external`, `charging`, `absent` ou `unknown`; `unavailable` quando o snapshot nao pode ser lido |
| `charge` | sinal decodificado do `CHG_STAT`: `charging`, `not_charging` ou `unknown` (leitura falha nunca vira `not_charging`) |
| `available` | `true` somente com leitura INA valida; `false` oculta a telemetria numerica (`null`) |
| `bus_voltage_mv`, `current_ma`, `percentage` | inteiros coerentes com o shell `battery protection status`; `null` quando indisponiveis |
| `protection_enabled` | opcao persistida (`battery protection on/off`) |
| `protection_active` | latch de corte ativo |
| `charger_enabled` | nivel comandado de `CHG_EN` (nao e a leitura do pad) |
| `sampled_at_us`, `sampled_age_ms` | timestamp da ultima amostra da politica e idade em ms; `null` se nunca amostrou; a idade fica abaixo do periodo do amostrador (~1 s) e pode variar entre leituras (dente-de-serra do timer da UI) |
| `fw_version` | `1.4.0` |
| `uptime_s` | cresce entre duas chamadas; queda indica reboot |

Duas chamadas seguidas devem manter `uptime_s` crescente e `sampled_age_ms`
pequeno (o timer de 1 s reamostra continuamente).

## Cenarios

### A. Sem cabo (Wi-Fi), bateria no pack

1. Desconecte o USB e mantenha o Wi-Fi conectado.
2. `curl -s http://<ip>/battery` duas vezes.
3. Esperado: `state` `battery`, `available` `true`, `percentage` plausivel para a
   tensao mostrada, `sampled_age_ms` menor que ~2000, `uptime_s` crescente.

### B. Fonte externa

1. Conecte a fonte/carregador (sem USB de dados).
2. Repita o `curl`.
3. Esperado: `state` `charging` ou `external`, `charge` coerente, `available`
   `true`. O `available` nao pode cair para `false` nem o percentual virar
   `null` com leitura INA valida.

### C. Latch de protecao / corte do carregador

1. Com `battery protection on` e o pack proximo do topo da faixa (charging e
   acima do limite de entrada de corte), observe `protection_active` virar
   `true` e `charger_enabled` ir para `false` na mesma resposta.
2. Ao desligar a protecao (`battery protection off`) ou ao cair para a faixa de
   liberacao, `protection_active` deve voltar a `false` e `charger_enabled` a
   `true`.
3. Nao force esse cenario apenas pelo endpoint: ele e read-only. Use o shell
   (`battery protection on|off|status`) e confirme que o JSON reflete a mesma
   politica.

## Checagens negativas e de contrato

```bash
# Cabecalhos: precisa trazer Content-Type application/json e Cache-Control: no-store
curl -sD - -o /dev/null http://<ip>/battery

# Metodo nao permitido: 405
curl -s -o /dev/null -w '%{http_code}\n' -X POST http://<ip>/battery

# Corpo limitado (sem newline): deve ter no maximo 384 bytes
curl -s http://<ip>/battery | wc -c
```

- `405` para metodo diferente de `GET`; `503` se outra requisicao estiver usando
  o mutex de requisicao (repita durante um `/screenshot`).
- O endpoint nao altera estado: depois de varias chamadas, `battery protection
  status` e o indicador do header devem continuar identicos.
- `/screenshot` e `/events.txt` continuam respondendo como antes.

## Evidencia a registrar

Data/hora, versao (`fw_version`), IP, comandos, respostas JSON completas,
`uptime_s` antes/depois, cabecalhos do `curl -sD -`, e qualquer reboot, panic ou
`Guru Meditation` observado. Ausencia de resposta, timeout ou queda de uptime e
falha do cenario.

## Limitacoes

Este roteiro prova a projecao HTTP e a coerencia com a politica; nao substitui a
validacao de bancada de tensao/corrente nem autoriza mudar limiares. O endpoint
nunca escreve `CHG_EN`; qualquer mudanca de latch continua vindo da politica do
firmware.
