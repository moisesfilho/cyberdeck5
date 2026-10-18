# Plano 07 — Comparação de ambientes

## Objetivo

Responder “o que mudou desde a última medição?” comparando snapshots locais de
Wi-Fi, conectividade e BLE, sem inferir identidade ou atividade além dos dados.

## Dependências

Depende de snapshots versionados de Wi-Fi survey, netdiag e BLE inspector; pode
usar o caderno para organização, mas deve permitir comparação local sem IA.

## Escopo

- Selecionar dois snapshots da mesma categoria e comparar entradas adicionadas,
  removidas e campos alterados.
- Wi-Fi: canal/segurança/RSSI; tratar RSSI como medição variável, não prova de
  deslocamento.
- BLE: preferir identidade consentida; não correlacionar endereços aleatórios
  entre sessões como se fossem identidade estável.
- Filtros para reduzir ruído e exportação solicitada explicitamente.

## Etapas e aceite

Definir igualdade/normalização por fonte; construir diff puro determinístico;
criar UI de revisão; testar versões diferentes, duplicatas, campos ausentes,
relógios divergentes e identificadores mascarados.

Aceite: comparação não modifica snapshots; incertezas são visíveis; nenhuma
associação de pessoa/dispositivo é inferida; retenção e exclusão ficam sob
controle do usuário.
