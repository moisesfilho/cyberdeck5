# Fase 5 — registro de release e manutenção do Tab5

**Estado em 05/11/2026:** o operador autorizou preparar `1.7.0` como candidata
a release, sem autorizar tag/publicação ainda. Os gates documentais anteriores
de `1.6.0` são históricos, não substituem os da nova imagem. `version.txt` é a
fonte da versão; pelo `AGENTS.md`, ela só é "liberada" quando houver tag
`vMAJOR.MINOR.PATCH` no commit que a introduziu.
Não criar tag sem pedido explícito.

## Base e proveniência

| Item | Evidência e limite |
| --- | --- |
| Alvo | M5Stack Tab5, ESP32-P4, ESP-IDF 6.1; versão candidata em `version.txt`: `1.7.0`. |
| Imagem observada | `docs/OS-PHASE3-BASELINE.pt-BR.md`: firmware `39953bf`, readback integral do único slot `factory` com SHA-256 igual ao build `5e355d17772c5fe62c47b637a6a672cc64ef4e177947adf024db3b25764f59c2`. O hash é de conteúdo, não autenticação criptográfica do boot. |
| Histórico de boot | Boot normal e checkpoint recuperado após safe mode; boot posterior à leitura eFuse também normal em `docs/OS-PHASE4-THREAT-MODEL.pt-BR.md`. Esses ensaios ocorreram antes deste registro documental: não atribuir automaticamente uma nova imagem ao HEAD de documentação. |
| Versão/release | `1.6.0` foi introduzida em `0917276`, mas a imagem validada inclui alterações funcionais até `39953bf`; nenhuma tag `v1.6.0` existe. Preparar `1.7.0` no commit candidato permite etiquetar o próprio commit que introduz essa versão, **somente após** validação/consentimento final. |

## Gates por mudança

1. Confirmar `git status`, `version.txt`, alvo IDF, hash do build e identidade
   do firmware instalado. Não inferir hash do dispositivo apenas de
   `sys.info.fw_version`. Registrar o commit de código e eventuais diffs
   funcionais da imagem; commits só documentais posteriores não mudam seu
   conteúdo.
2. Executar `make -C tests/host/keymap test`,
   `make -C tests/host/keymap verify`, `idf.py build` com ESP-IDF exportado e
   `git diff --check`. Registrar resultado, ambiente e falhas sem marcar gate
   `done` a partir de um subconjunto de testes.
3. Para **cada mudança funcional**, validar no Tab5 os fluxos afetados após
   build/flash aprovado, conforme `AGENTS.md`, com efeito observado (não apenas
   ACK da ponte), versão/hash e registro de reboot/panic. Mudança só documental
   não requer flash; aproveitar evidência física prévia somente para o mesmo
   conteúdo de firmware e cenário, sem declarar cenários adicionais aceitos.
4. Atualizar plano, dashboard, mapa de código e roteiros/evidências relevantes.
   Aplicar a política de versão do `AGENTS.md`; não etiquetar release sem
   solicitação explícita.

## Limitações conhecidas e decisão de produto

- O operador aceitou o funcionamento dos contratos implementados no uso pessoal
  observado. Fase 3 continua `active` para aceite técnico integral; isso **não**
  impede apps simples e confiáveis, mas não substitui orçamento de stack/RAM ou
  teste físico dos fluxos de uma app nova.
- SD é obrigatório para o boot. Sem ele, `sd_mount` abortou e houve reinícios
  automáticos (journal chegou a 65 interrupções) antes da recuperação em safe
  mode. O operador aceita não iniciar e reiniciar tentando montar o cartão
  obrigatório: **não é defeito nem bloqueio de release** nesse escopo. Não
  repetir o ensaio físico SD/NVS sem limitar um único boot e evitar
  reenergização acidental. Uma tela informando que falta o cartão seria
  melhoria opcional; a UI atual só inicia após o mount.
- Uma consulta `music.status` após pause/resume não recebeu resposta, embora a
  seguinte tenha respondido sem reboot. Primeiras consultas Serial-JTAG após
  reconexão USB também perderam resposta; causa não demonstrada. Não registrar
  esses fluxos como livres de defeitos. Confirmação humana de speaker/jack e
  budgets de outras tasks ainda não constam da evidência instrumentada.
- Fase 4/OS-TAB5-06 foi aceita apenas no modelo pessoal: LAN controlada,
  manutenção USB, apps compiladas confiáveis, riscos de HTTP sem auth/TLS e
  credenciais em claro no SD aceitos. Isso não prova proteção contra peers na
  LAN ou acesso físico; um chat com IA exige revisão própria de dados e chaves.

## Rodada de gates desta entrega documental — 05/11/2026

| Gate | Resultado |
| --- | --- |
| Host | `make -C tests/host/keymap test`: PASS, suíte agregada completa, inclusive contrato IMU; sem `clean` nem execução concorrente de cobertura. |
| LVGL shim | `make -C tests/host/keymap verify`: PASS; `LV_KEY_*` iguais aos do LVGL gerenciado. |
| Dashboard | `make -C tests/host/keymap test_os_dashboard_contract`: PASS após atualizar plano e estados. |
| Firmware | `source /home/moises/esp/esp-idf/export.sh && idf.py build`: PASS, ESP-IDF 6.1/esp32p4, app `0x27a6e0` com `0x185920` bytes (38%) livres na partição de 4 MiB. Bootloader `0x5b00`, margem de `0x500` bytes (5%); observar essa margem em mudanças futuras. |
| Imagem | SHA-256 de `build/cyberdeck5.bin` após build `5e355d17772c5fe62c47b637a6a672cc64ef4e177947adf024db3b25764f59c2`, igual ao readback documentado da imagem `39953bf`. Identidade de bytes, não atestação de segurança. |
| Diferenças | `git diff --check`: PASS. Conferir também `git diff --cached --check` antes do commit. |
| Hardware | Sem flash nesta entrega exclusivamente documental; evidências anteriores da mesma imagem em Fases 3–4. Não se executaram novos cenários físicos, não se atribui ao HEAD documental um firmware novo e não se reclassificam pendências técnicas como resolvidas. |

A Fase 5 permanece `active`: não há tag `v1.6.0` nem `v1.7.0`. A rodada
documental de `1.6.0` não mudou firmware; a candidata `1.7.0` exige nova
rodada host/build, validação física e decisão humana sobre a tag.

## Candidata `1.7.0` — preparação (05/11/2026)

- Base funcional: código de `39953bf` (após esse commit só houve alterações em
  documentação/evidências); alteração de firmware desta candidata limitada à
  versão em `version.txt`. Nenhuma versão de app/`api_version` foi alterada.
- Testes focados `test_battery_http_contract`,
  `test_battery_protection_contract` e `test_os_dashboard_contract`: PASS.
  `idf.py build` ESP-IDF v6.1/esp32p4: PASS, app `0x27a6e0`, partição
  `0x400000`, 38% livre; bootloader `0x5b00`, margem `0x500`. SHA-256 do app
  `d4404708764ce6d8d49050ded73c35a86a93ad346e7dd96cbbc6f7cbeeca241a`.
  `esptool image-info` confirma `App version: 1.7.0`, alvo ESP32-P4 e
  `ESP-IDF: v6.1` na imagem compilada. Linker emitiu aviso de `kill` não
  implementado na dependência libssh; a compilação concluiu.
  `make -C tests/host/keymap verify`: PASS (paridade LVGL shim).
- `make -C tests/host/keymap test`: PASS, suíte host completa, incluindo
  contrato IMU; sem `clean` ou execução concorrente de cobertura.
- Commit que introduziu `1.7.0`: `912e291` (sem tag). Rebuild após commit
  preservou SHA-256 `d4404708764ce6d8d49050ded73c35a86a93ad346e7dd96cbbc6f7cbeeca241a`.
  `idf.py -p /dev/ttyACM0 flash`: PASS no Tab5 ESP32-P4 rev. v1.3;
  bootloader/partições verificados, app regravado apenas nos setores alterados,
  `Hash of data verified` e hard reset. Dados de identificação USB/endereços
  de rede não são publicados aqui.
- Readback **somente** do app `factory` a partir de `0x10000` por
  `0x27a6e0` bytes via `esptool read-flash`: SHA-256 do arquivo lido
  `d4404708764ce6d8d49050ded73c35a86a93ad346e7dd96cbbc6f7cbeeca241a`,
  idêntico ao binário local. Dump privado
  `/tmp/opencode/cyberdeck5-1.7.0-readback-20261105.bin` (não versionado).
  A ferramenta reiniciou o chip ao finalizar; `sys.info` após flash (uptime
  `00:00:06`) e após readback (uptime `00:00:07`) respondeu `fw_version=1.7.0`,
  `idf_version=v6.1`, `safe_mode=false`, `interrupted_boots=0`,
  `recovery_persisted=true`. Não houve queda de uptime entre a leitura pós-
  readback e a leitura posterior `00:00:21`.
- `screen.wake` respondeu normalmente; `ui.dump` confirmou texto do header
  `CYBERDECK5 1.7.0`; `ui.type pwd` + `term.dump` confirmou `/` e o prompt
  local; `GET /battery` pela LAN respondeu HTTP 200, schema 1,
  `fw_version=1.7.0` (dados de rede omitidos). Esses são os cenários físicos
  afetados pela mudança de versão; playback, speaker/jack e novas falhas
  físicas **não** foram revalidados nem declarados aprovados por inferência.
- Pendente: autorização humana **separada** para tag `v1.7.0` apontar para
  `912e291` e eventual publicação de release. Os defeitos/limites da Fase 3
  permanecem registrados; esta candidata não os corrigiu.
