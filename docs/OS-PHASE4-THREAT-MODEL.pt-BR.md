# Fase 4 — modelo de ameaça preliminar do Tab5

**Estado:** inventário e fronteiras documentados em 05/11/2026; controles
propostos não aprovados nem habilitados por este documento. A Fase 3 e a
validação física do firmware `1.5.0` continuam abertas. Não usar o inventário
como certificação de segurança ou autorização para gravar eFuses, apagar SD/NVS
ou trocar o processo de atualização.

## Escopo, ativos e adversários

O produto é um dispositivo pessoal ESP32-P4/Tab5 com firmware e apps compiladas,
SD removível, USB Serial-JTAG e conectividade Wi-Fi/BLE/SSH. Ameaças consideradas:
outro dispositivo na mesma LAN, pessoa com acesso físico/USB/SD e entrada não
confiável em dados remotos/arquivos. Apps de terceiros instaláveis, isolamento
entre processos e adversário com execução nativa já obtida **não** fazem parte
das garantias atuais. O operador controla o ambiente e decide quais riscos
aceitar; nada aqui pressupõe que toda LAN privada seja confiável.

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
  controles. Isto caracteriza a configuração inspecionada, **não atesta eFuses
  ou o hash da imagem em execução**; a confirmação no dispositivo é pendente.

## Riscos não cobertos e decisões a tomar

| Opção (não aplicada) | Requisito que justificaria | Custo/impacto e recuperação a definir antes de aprovar |
| --- | --- | --- |
| Autenticar e proteger HTTP ou desligar rotas sensíveis | LAN compartilhada, acesso à tela/logs restrito a operador identificado | Gestão de credencial/certificado, UX e clientes existentes; provar revogação e restauração de acesso sem deixar a interface pública por acidente |
| Proteger credenciais no SD/NVS ou retirar segredos do SD removível | Confidencialidade contra perda/acesso físico ao cartão | Provisão e backup de chaves, migração do `wifi.cfg`, perda de chave e recovery offline; modo `0600` sozinho não resolve |
| Secure Boot, flash encryption e eventual NVS encryption | Exigir integridade de boot e/ou proteção contra extração física da flash | eFuses/keys podem impor efeitos irreversíveis, mudar desenvolvimento/flash e inviabilizar recuperação; documentar chave, backup, rollback/brick e testar em dispositivo separado com autorização explícita |
| Processo de atualização autenticado e recuperável | Releases remotos ou firmware reproduzível com rollback | Layout atual é `factory` único; desenho de partições/OTA ou fluxo USB assinado, compatibilidade de dados, imagem de recuperação e teste de rollback antes de migrar |
| Restringir operações da bridge USB | USB acessível a pessoas sem autorização de manutenção | Separar diagnóstico de escrita/recovery, provisionar credenciais ou desabilitar bridge em builds específicos; garantir recuperação de manutenção sem desbloqueio inseguro |

Essas opções têm trade-offs que dependem de decisão do operador sobre acesso
físico, rede confiável, disponibilidade, perda aceitável de dados e método de
recuperação. **Não** converter riscos em mudanças de firmware automaticamente.
Se o requisito passar a incluir apps independentes/não confiáveis, reabrir a
arquitetura do plano para comparar isolamento suportado pela plataforma com
Zephyr/NuttX ou outra plataforma antes de discutir PMP, loader ou syscalls;
grants atuais não atendem esse requisito.

## Evidências e gates ainda pendentes

1. Confirmar configuração do build que será efetivamente gravado, sua
   identidade/hash e (se relevante) estado de eFuses no Tab5 por procedimento
   não destrutivo e autorizado. O `sys.info` atual não atesta a imagem.
2. Aprovar modelo de adversário, riscos aceitos e controles selecionados com o
   operador. Não considerar OS-TAB5-06 aceito só pelo inventário documental.
3. Para cada controle aprovado, escrever plano de migração/recuperação e
   validar no dispositivo correspondente antes de marcar Fase 4 concluída.
