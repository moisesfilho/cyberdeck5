# Fase 4 — modelo de ameaça do Tab5

**Estado em 05/11/2026:** Fase 4 e OS-TAB5-06 concluídos **no escopo pessoal
aprovado**; o operador aceitou explicitamente os riscos residuais abaixo.
Nenhum controle foi habilitado por este documento. A Fase 3 permanece aberta
para aceite técnico integral, com uso cotidiano aceito pelo operador. Este
modelo não certifica segurança nem autoriza gravar eFuses, apagar SD/NVS ou
mudar o processo de atualização.

## Escopo, ativos e adversários

O produto é um dispositivo pessoal ESP32-P4/Tab5 com firmware e apps compiladas,
SD removível, USB Serial-JTAG e conectividade Wi-Fi/BLE/SSH. O uso aprovado
nesta fase é **somente em LAN sob controle do operador**, com manutenção e
atualização locais por USB. Acesso físico de terceiros ao Tab5/USB/SD fica
fora do adversário adotado; não se afirma proteção caso ele ocorra. Peers na
LAN, tráfego interceptável e entradas remotas/arquivos malformados continuam
sendo fontes de risco: controle da rede não autentica cada peer nem torna o
HTTP confidencial. Apps de terceiros instaláveis, isolamento entre processos
e adversário com execução nativa já obtida **não** fazem parte das garantias.
O dispositivo tem um operador, uso intermitente (nem sempre conectado) e apps
simples compiladas e confiáveis, como música, notas e eventual chat com IA.
Um chat futuro precisa de revisão própria de dados enviados ao serviço externo,
credenciais, conectividade e custo antes de ser considerado coberto por este
aceite; o uso pessoal não torna automaticamente esses dados privados.

**Aceite explícito de risco pelo operador (05/11/2026):** manter nesta etapa
HTTP sem autenticação/TLS, credenciais Wi-Fi em texto claro no SD e Secure
Boot/criptografia desativados. Isso é restrito ao ambiente acima, não uma
alegação de proteção contra peers na LAN, acesso físico, extração de segredos
ou alteração de firmware. Se mudar a rede, a custódia do dispositivo ou a
necessidade de OTA, reavaliar controles antes de operar sob o novo cenário.
A dependência do SD para boot é requisito do produto: sem ele, não iniciar e
reiniciar tentando novamente o mount são consequências aceitas pelo operador,
não problema de segurança a resolver nesta fase. O ensaio histórico e o risco
de repetir um teste físico sem controle ficam documentados na Fase 3. Uma
tela avisando que o sistema exige cartão seria melhoria opcional de UX, não
uma proposta de tornar o SD opcional.

| Ativo | Entrada/fronteira | Controle observado e limite | Consequência se comprometido |
| --- | --- | --- | --- |
| Credencial Wi-Fi e configuração | `wifi_storage.cpp`, SD (`WIFI_CFG_PATH`) | Arquivo gravado com modo `0600` e senha em texto claro; permissão do VFS não protege cartão removido | Exposição de acesso à rede ao leitor do SD |
| Host keys e sessão SSH | `ssh_client.cpp`, `cyberdeck_known_hosts_file.cpp`, SD e host remoto | Comparação de host key e confirmação de chave desconhecida; arquivo bounded e `O_NOFOLLOW` na leitura. Aceite inicial pelo operador não autentica host automaticamente | MITM no primeiro contato se chave falsa for aceita; perda do histórico de confiança com SD comprometido |
| Bonds/estado BLE e recovery | store BLE/NimBLE e NVS | Política de bonds e recovery exercitada por testes host; `sdkconfig` local não habilita criptografia NVS | Leitura/manipulação física de estado, degradação de recovery ou perda de bonds |
| Dados do SD, logs e conteúdo de tela | shell local, editor, player, `fs.write`, `events.read`, HTTP | Shell local confinado à raiz virtual do SD; `fs.write` tem caminho/tamanho limitados. Não há isolamento físico do SD nem garantia de confidencialidade de logs/tela | Leitura, adulteração ou divulgação de dados e contexto de sessão |
| Firmware/boot e disponibilidade | flash, boot, poweroff, HTTP, Serial-JTAG | `partitions.csv` declara app `factory` e não declara slot OTA; `sdkconfig` atual tem Secure Boot e flash encryption desligados | Firmware não autenticado/modificado, impossibilidade de boot ou perda de disponibilidade |

## Superfícies de entrada e controles reais

- **LAN:** `screenshot_server.cpp` registra `GET /screenshot`, `/events.txt` e
  `/battery` na porta HTTP 80 enquanto Wi-Fi está habilitado e possui IP.
  `is_local_peer` aceita loopback, faixas IPv4 privadas/link-local e IPv6
  loopback/ULA/link-local (inclusive IPv4-mapped); outras origens recebem 403.
  Não há autenticação nem TLS nos três handlers. Um peer permitido pode ler
  captura da tela, projeção de eventos e telemetria. Filtro por endereço de
  origem **não** identifica o usuário, não impede interceptação na LAN e não
  substitui autorização. O snapshot de bateria é somente leitura, mas pode
  revelar estado/versão e contribuir para reconhecimento.
- **USB físico:** a bridge em `cyberdeck_serial_bridge.cpp` aceita NDJSON com
  parser bounded e comandos de UI, `events.read`, `fs.write` no SD e
  `sys.safe_mode.clear`; a lista de comandos não exige credencial. `ui.type`
  permite executar comandos do shell no contexto local. Proteção de tela é
  controle de interação/visibilidade, não autenticação do acesso USB ou da
  bridge. Portanto acesso à porta equivale a capacidade operacional elevada;
  não tratar o USB como console de observação apenas.
- **SD e dados externos:** confinamento de paths, `O_NOFOLLOW` onde aplicado,
  validações de tamanho e leases reduzem traversal, corrupção acidental e
  certas entradas malformadas. Não protegem confidencialidade de um cartão
  removido, não autorizam apps nativas não confiáveis e não garantem que todo
  writer legado participe da mesma lease (ver `OS-RESOURCE-CONTRACTS.pt-BR.md`).
- **Apps e rede de saída:** grants/quota da runtime são verificados
  cooperativamente nas facades; editor usa facade, enquanto music/netdiag têm
  adapters concretos de plataforma. Isso limita ações de apps **compiladas e
  cooperativas**, não isolamento de memória, CPU, drivers ou de código hostil.
  SSH/BLE têm verificação de identidade/pareamento próprios; não tornam
  seguros os endpoints HTTP nem o armazenamento local.
- **Atualização/configuração:** README documenta `idf.py ... flash monitor` via
  USB; `partitions.csv` contém só `factory`, sem entradas `ota_0/ota_1` ou
  `otadata`. Não foi encontrada rota OTA nas apps inspecionadas. O build local
  em `sdkconfig` declara `# CONFIG_SECURE_BOOT is not set`,
  `# CONFIG_SECURE_FLASH_ENC_ENABLED is not set` e
  `# CONFIG_NVS_ENCRYPTION is not set`. `sdkconfig.defaults` não habilita esses
  controles. Isso caracteriza a configuração inspecionada; a verificação
  independente das eFuses e a identificação da imagem estão abaixo.

## Verificação não destrutiva do build e dispositivo — 05/11/2026

- `sdkconfig`: Secure Boot, flash encryption, NVS encryption, HTTPS server e
  rollback de app desativados. `sdkconfig.defaults` não habilita esses
  controles. `partitions.csv` declara somente `factory` como partição de app,
  sem `otadata`/slots OTA. Flash USB local com backup e readback basta para o
  uso aprovado, mas não fornece rollback automático ou autenticidade
  criptográfica de firmware.
- Readback integral anterior na Fase 3 identificou imagem `39953bf`, versão
  `1.6.0`; SHA-256 de `build/cyberdeck5.bin`
  `5e355d17772c5fe62c47b637a6a672cc64ef4e177947adf024db3b25764f59c2`.
  A leitura eFuse não autentica a imagem nem prova que ela não mudou depois.
- Leitura **somente leitura** com `python -m espefuse --chip esp32p4 -p
  /dev/ttyACM0 summary --format json`: `SECURE_BOOT_EN=false`,
  `SPI_BOOT_CRYPT_CNT=Disable` e `DIS_DOWNLOAD_MODE=false`, todos com valor
  bruto `0x0`. Nenhuma eFuse foi gravada. Resumo privado
  `/tmp/opencode/phase4-efuse-summary-20261105.json`, SHA-256
  `2cb45a5afae626c64b090d317ba58d36f443646778c82c95d7810408db6def2e`.
  Não publicar dumps eFuse ou logs seriais brutos no repositório.
- Após a leitura, a primeira consulta serial não respondeu e HTTP no IP
  anterior estava indisponível. Um reset controlado com SD instalado via
  `idf.py monitor` registrou `sd_mount=ESP_OK`, checkpoint recovery persistido
  com `safe_mode=0`, `pending=0`, `interrupted=0` e
  `app_main_complete boot_ready=1`, sem marcador de panic no log capturado.
  O exit 124 foi o timeout esperado do monitor. Log privado
  `/tmp/opencode/phase4-efuse-post-reset-boot-20261105.log`, SHA-256
  `c0bffe660c7fc7e0cecef362677f71679623da6b873a38fff2a876c518f971e3`.
  `sys.info` posterior: versão `1.6.0`, uptime `00:00:25`,
  `safe_mode=false`, `interrupted_boots=0`, `recovery_persisted=true`.
  Não atribuir a resposta ausente à leitura eFuse sem diagnóstico.

## Controles avaliados e gatilhos de revisão

| Opção (não aplicada) | Requisito que justificaria | Custo/impacto e recuperação a definir antes de aprovar |
| --- | --- | --- |
| Autenticar e proteger HTTP ou desligar rotas sensíveis | LAN compartilhada, acesso à tela/logs restrito a operador identificado | Gestão de credencial/certificado, UX e clientes existentes; provar revogação e restauração de acesso sem deixar a interface pública por acidente |
| Proteger credenciais no SD/NVS ou retirar segredos do SD removível | Confidencialidade contra perda/acesso físico ao cartão | Provisão e backup de chaves, migração do `wifi.cfg`, perda de chave e recovery offline; modo `0600` sozinho não resolve |
| Secure Boot, flash encryption e eventual NVS encryption | Exigir integridade de boot e/ou proteção contra extração física da flash | eFuses/keys podem impor efeitos irreversíveis, mudar desenvolvimento/flash e inviabilizar recuperação; documentar chave, backup, rollback/brick e testar em dispositivo separado com autorização explícita |
| Processo de atualização autenticado e recuperável | Releases remotos ou firmware reproduzível com rollback | Layout atual é `factory` único; desenho de partições/OTA ou fluxo USB assinado, compatibilidade de dados, imagem de recuperação e teste de rollback antes de migrar |
| Restringir operações da bridge USB | USB acessível a pessoas sem autorização de manutenção | Separar diagnóstico de escrita/recovery, provisionar credenciais ou desabilitar bridge em builds específicos; garantir recuperação de manutenção sem desbloqueio inseguro |

O operador optou por **não aplicar** essas opções nesta etapa sob as premissas
acima. Não converter riscos aceitos em mudanças de firmware automaticamente;
se a premissa mudar, definir requisito e recuperação antes de implementar.
Se o requisito passar a incluir apps independentes/não confiáveis, reabrir a
arquitetura do plano para comparar isolamento suportado pela plataforma com
Zephyr/NuttX ou outra plataforma antes de discutir PMP, loader ou syscalls;
grants atuais não atendem esse requisito.

## Limites do aceite

1. O aceite vale só para LAN controlada, custódia física e flash USB local;
   não prova confidencialidade nem segurança contra outros peers da LAN.
   Filtro de IP não é autenticação.
2. Configuração e eFuses foram verificadas, e o boot posterior observado;
   **não** foram testados ataques, migração de controles ou recuperação após
   adulteração. Nenhum controle novo foi instalado nesta fase.
3. Fase 3 e seus demais gates técnicos físicos/budgets seguem abertos;
   não se exige boot sem SD nem correção de reinícios ao faltar o cartão
   obrigatório. Controles futuros exigem plano próprio de migração,
   backup, recuperação e validação antes de marcar seu aceite.
