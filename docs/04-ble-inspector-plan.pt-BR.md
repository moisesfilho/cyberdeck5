# Plano 04 — Inspetor Bluetooth LE

## Objetivo

Evoluir `bluetooth search/paired` para inventário BLE e inspeção GATT explícita
de dispositivo selecionado pelo usuário.

## Estado atual

O Tab5 usa ESP32-C6 via ESP-Hosted. O firmware já tem scan LE, pareamento, bonds
bounded, reconexão e descoberta HID básica; o modelo não armazena chaves.
Bluetooth Classic/A2DP não está implementado.

## Escopo

- Anúncios: nome sanitizado, endereço/tipo conforme política de privacidade,
  RSSI, appearance, serviços anunciados e fabricante quando disponível.
- Histórico de RSSI somente durante a sessão.
- Após seleção e consentimento, conectar e listar serviços, características,
  propriedades e descritores GATT com limites de quantidade, bytes e tempo.
- Exportação opcional sanitizada. Leituras de característica ficam fora da
  primeira versão ou exigem ação explícita por característica.

## Não objetivos

Não escrever características, controlar dispositivos, coletar HID reports,
capturar chaves, emparelhar desconhecidos automaticamente, fazer spam ou
implementar Bluetooth Classic.

## Etapas e aceite

Revisar APIs NimBLE, dispatch e autorização; definir limites; estender snapshots
bounded e sanitizados preservando `addr_type`, geração e rejeição stale;
implementar descoberta GATT read-only/cancelável; testar anúncios malformados,
overflow, desconexão, timeout, callbacks tardios e endereços rotativos; validar
com periférico controlado.

Aceite: scan/teclado BLE não regridem; GATT só inicia após seleção/consentimento;
cancelamento libera recursos; diagnóstico não registra passkey, IRK, LTK ou
valores de atributo por padrão.
