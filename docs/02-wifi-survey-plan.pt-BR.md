# Plano 02 — Levantamento Wi-Fi passivo

## Objetivo

Evoluir o scan existente para `wifi survey`: apresentar redes anunciadas ao
alcance, comparar intensidade/canal e exportar medições, sem associar-se a redes
nem transmitir sondagens dirigidas.

## Estado atual

`wifi_mgr` já realiza scans e a UI oferece `wifi search`; `wifi audit` captura a
associação atual. Reutilizar dispatch, snapshots e ownership, sem duplicar o
controlador de rádio.

## Escopo

- Lista ordenada por RSSI com SSID (inclusive oculto), BSSID, canal, segurança e
  intensidade; valores desconhecidos permanecem explícitos.
- Atualização manual e sessão curta de amostragem com intervalo dentro de
  limites fixos; tendência de RSSI apenas durante a sessão.
- Exportação bounded para o SD somente após confirmação.
- Aviso se o scan interromper ou degradar conectividade ativa.

## Limites e etapas

Não coletar payloads, fazer deauth/spoofing, associação automática, probes
direcionados ou wardriving nesta entrega. Especificar campos e limites; integrar
scan via snapshots bounded, cancelamento e tokens; implementar UI e exportação;
testar redes vazias/ocultas, duplicatas, overflow, cancelamento, queda de Wi-Fi e
persistência interrompida; validar com e sem associação ativa.

## Aceite e privacidade

Exibir somente anúncios recebidos; lista/arquivo bounded; nunca exportar
automaticamente; ignorar callbacks stale; SSH e associação existente continuam
funcionais ou a limitação é indicada. SSID/BSSID podem identificar locais: não
reter histórico entre boots por padrão e oferecer mascaramento na exportação.
