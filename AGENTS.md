# Instrucoes para agentes

## Mapa do codigo

- Antes de iniciar buscas exploratorias no repositorio, leia `code-map.md`.
- Use o `code-map.md` para identificar os modulos, arquivos, simbolos,
  dependencias e testes relacionados antes de pesquisar diretamente no codigo.
- Confirme no codigo atual qualquer informacao relevante encontrada no mapa;
  o mapa e um indice de navegacao, nao substitui a fonte de verdade.
- Ao alterar qualquer arquivo existente ou criar novos arquivos, atualize
  `code-map.md` no mesmo trabalho.
- Mantenha o mapa consistente com os caminhos, responsabilidades, simbolos e
  relacoes entre producao e testes atuais.
- Nao inclua artefatos gerados no mapa, exceto quando forem necessarios para
  documentar uma dependencia ou comando de validacao.

## Politica de busca progressiva

Siga esta ordem para localizar codigo, evitando buscas globais prematuras:

1. **Consultar o indice:** leia `AGENTS.md` e `code-map.md`; identifique o
   modulo, o diretorio e os testes mais provaveis.
2. **Inspecionar arquivos direcionados:** leia primeiro os arquivos indicados
   no mapa e consulte seus imports, simbolos e dependencias diretas.
3. **Pesquisar dentro do modulo:** se os arquivos nao forem suficientes, use
   `grep` ou `glob` exclusivamente no diretorio identificado.
4. **Ampliar para modulos relacionados:** examine interfaces, chamadas,
   implementacoes e testes de integracao somente quando a tarefa exigir.
5. **Justificar a busca global:** se a localizacao continuar incerta, permita
   uma pesquisa mais ampla, limitada por extensoes, pastas e padroes
   relevantes. Nao use busca global como primeiro passo.

## Escopo das alteracoes

- Preserve alteracoes existentes no worktree que nao pertencam a tarefa atual.
- Prefira a menor alteracao correta e valide referencias e testes relacionados
  quando o trabalho estiver concluido.

## Acompanhamento do plano OS Tab5

- Quando uma entrega avancar itens de `docs/OS-IMPLEMENTATION-PLAN.pt-BR.md`,
  recortes de `docs/OS-TAB5-REQUIREMENTS.pt-BR.md` ou criterios OS-TAB5,
  atualize `docs/OS-IMPLEMENTATION-DASHBOARD.html` no mesmo trabalho.
- Edite os estados no bloco `const dashboard` com base nas evidencias; `active`
  nao conta como `done`. Recorte documentado, teste host e validacao fisica
  devem permanecer distintos. Nao marque aceite no Tab5 sem evidencias do
  firmware em execucao e do cenario correspondente.
- Execute `make -C tests/host/keymap test_os_dashboard_contract` para verificar
  paridade de checkboxes, IDs dos criterios e links com o plano vigente.

## Consistencia de documentacao e limiares

- Ao mudar um limiar, uma constante de dominio ou uma janela (tensao de corte,
  faixa de percentual, capacidade, versao), varra o valor antigo antes de
  fechar o trabalho:

  ```bash
  grep -rn "<valor antigo>" README.md README.pt-BR.md code-map.md \
      docs/ tests/manual/ components/cyberdeck/include/
  ```

- Os arquivos que precisam ficar coerentes com a constante sao: `README.md`,
  `README.pt-BR.md`, `docs/ARCHITECTURE.md`, `docs/ARCHITECTURE.pt-BR.md`,
  `code-map.md` (inclusive a matriz de rastreabilidade `REQ/AC -> TEST`),
  `tests/manual/*.pt-BR.md` e os espelhos em `tests/host/keymap/contracts/`.
- A documentacao descreve a constante, nao uma copia independente dela: quando
  o valor aparecer em texto, o teste deve derivá-lo da fonte (header,
  `version.txt`, Makefile) em vez de pinar o literal.
- Preserve mencoes historicas explicitas (por exemplo, "o release anterior era
  85 %"), desde que a frase deixe claro que o valor e antigo.

## Memoria de projeto

- `.opencode/memory/` e memoria derivada, nao fonte de verdade. Ao mudar
  limiar, janela, versao ou fluxo, atualize o bloco correspondente no mesmo
  trabalho e considere o codigo como referencia final.

## Versionamento do firmware e aplicativos

- A versão do firmware tem exatamente três segmentos decimais sem zero-padding,
  `MAJOR.MINOR.PATCH`; o valor inicial é exatamente `1.0.0`. `version.txt` é sua
  única fonte. O ESP-IDF a propaga para `PROJECT_VER` e `esp_app_desc_t::version`,
  consumida pela UI; não duplique nem fixe a versão em código.
- `MAJOR` é controle pessoal do usuário e nunca é incrementado automaticamente;
  só muda por solicitação explícita. Mudanças diretas no firmware/base incrementam
  `MINOR`; cada mudança funcional de app incrementa `PATCH` e a versão própria
  desse app no manifesto, preservando `api_version`.
- Mudanças que afetem mais de um escopo incrementam todos os segmentos
  correspondentes. Segmentos podem ter mais de dois dígitos, desde que caibam no
  campo `esp_app_desc_t::version` e no título da UI. Se o próximo incremento não
  couber nesses limites técnicos, pare e solicite orientação; não faça rollover
  nem altere outro segmento como substituição.
- Versões dos manifests de apps seguem o semver próprio do app e não derivam da
  versão do firmware.
- "Liberado" significa que existe a tag `vMAJOR.MINOR.PATCH` no commit que
  introduziu a versão. Enquanto a versão corrente não tiver tag, mudanças
  funcionais podem compartilhar o mesmo `MINOR` (o binário validado no
  dispositivo segue como referência); depois da tag, a próxima mudança
  funcional incrementa `MINOR`. Não crie tag sem pedido explícito.
- Testes e contratos não pinam a versão exata: validam o formato
  `MAJOR.MINOR.PATCH` (sem zero-padding) e um piso mínimo por entrega, para que
  o próximo incremento não quebre o agregado nem a CI.

## Execucao dos testes host

- Os alvos ficam em `tests/host/keymap/Makefile`. Para um teste focado, execute
  `make -C tests/host/keymap <alvo>`; para o agregado completo, use
  `make -C tests/host/keymap test` (inclui os cenarios e gates registrados no
  Makefile, inclusive cobertura/scope guard e contratos).
- Os alvos migrados usam artefatos `<alvo>_bin` listados em `BINS`; os demais
  mantêm o nome original. O alvo público `<alvo>` é `.PHONY`, depende do
  artefato e sempre executa o cenário, mesmo
  quando o binário já está atualizado. O build é incremental: uma segunda
  execução de `make -C tests/host/keymap test` sem mudanças não recompila, e
  alterações em fontes ou headers recompilam os alvos afetados. Os executáveis
  `<alvo>_bin` ficam na raiz de `tests/host/keymap/`. Nos alvos com compilação
  incremental por TU, os diretórios `test_<alvo>/` contêm objetos `.o`, depfiles
  `.o.d` e sidecars gcov. Os alvos monolíticos não têm diretório por alvo: seus
  sidecars gcov ficam na raiz, com nomes como `<alvo>_bin-<tu>.gcda`. Esses
  artefatos são cobertos pelo `.gitignore`.
- `-jN` paraleliza somente a construção; sem essa opção, ela é serial. O Makefile
  preserva o jobserver/opção do chamador. A execução dos cenários permanece
  ordenada e serial para evitar concorrência entre cenários e artefatos gcov.
  A CI aplica gcovr mínimo de 80% linhas, 90% funções e 50% branches, além do
  scope guard de TUs; alvos focados não executam esses gates globais.
- Não execute alvos que escrevem cobertura concorrentemente no mesmo diretório.
  Use `make -C tests/host/keymap clean` antes de uma rodada de cobertura limpa;
  `clean` remove os executáveis listados em `BINS`, nomes legados listados em
  `LEGACY_TEST_ARTIFACTS`, diretórios de objetos/depfiles dos alvos incrementais
  e sidecars gcov na raiz (`*.gcda`, `*.gcno`, `*.gcov`). Não rode `clean` durante uma execução. Para verificações
  incrementais, chame apenas o alvo focado necessário. Alguns alvos de receita
  monolítica só reagem a headers transitivos declarados como pré-requisitos;
  rastreio fino por TU via depfiles vale para os alvos já convertidos. A CI
  executa `clean` antes, portanto os gates de cobertura não dependem desse
  rastreio incremental.
- Contratos do harness nao fixam valores instantaneos. Verifique a propriedade
  (unicidade, presenca do alvo por nome, `<alvo>_bin` do alvo novo) em vez da
  contagem literal de `BINS`, do total de TUs ou da matriz do baseline: quando
  a contagem muda de forma intencional, o ajuste e derivado do proprio
  Makefile, nunca um literal reescrito a mao.
- `test_incremental_final_contract` compara o wiring real com
  `incremental_final_baseline.json` e depende de objetos atualizados. Em
  arvore com objetos antigos ele pode acusar divergencia que nao existe; rode
  `make -C tests/host/keymap test-build` antes de usa-lo de forma focada, ou
  valide pelo agregado com `make -C tests/host/keymap clean test`. Falha nesse
  contrato sem build atualizado nao e, por si so, evidencia de defeito de
  wiring.

## Validacao de firmware no dispositivo

### Atualizacao via Wi-Fi (OTA)

O layout `partitions.csv` reserva `factory`, `ota_0`, `ota_1` (4 MiB cada) e
`otadata` em flash de 16 MiB. A migracao do layout antigo (`factory` unico)
exige **um flash inicial por USB** com `idf.py flash`; OTA nao altera tabela de
particoes nem bootloader. Faca backup do estado persistente antes dessa migracao
e confirme o boot, a NVS e o Wi-Fi no novo layout. Nao apague a NVS para
"consertar" um boot sem diagnostico.

Provisione o segredo da unidade **uma vez pela USB Serial-JTAG**, apos instalar
o firmware com OTA (requer `pyserial` no computador):

```bash
python3 tools/update_firmware.py --provision --port /dev/ttyACM0
```

O script armazena `~/.config/cyberdeck5/ota.key` com permissao 0600. Nao
imprima, commite, anexe a logs nem compartilhe esse arquivo. A rota `ota.key`
existe **somente** na ponte USB; nao a encaminhe pela LAN, `ui.type` ou HTTP.
Para atualizar sem cabo, com o Tab5 ligado ao Wi-Fi e o binario ESP32-P4 ja
compilado:

```bash
python3 tools/update_firmware.py --host <IP-DO-TAB5> --firmware build/cyberdeck5.bin --confirm
python3 tools/update_firmware.py --host <IP-DO-TAB5> --firmware build/cyberdeck5.bin --silent
```

`--confirm` envia primeiro apenas metadados autenticados e espera aprovacao
**fisica no Tab5** (Enter ou botao Confirmar; Esc/Cancelar recusa, prazo 120 s);
somente apos a aprovacao transmite o `.bin`. `--silent` mantem envio direto sem
interacao. Ambos autenticam nonce de uso unico, modo, versao, tamanho e SHA-256
com HMAC-SHA256. O servico esta na porta HTTP 8080, endpoints
`GET /ota/challenge`, `POST /ota/prepare` (metadados, corpo vazio), `POST /ota`
(corpo `.bin` bruto) e `GET /ota/status`; a imagem deve ser do projeto
`cyberdeck5`, alvo ESP32-P4 e versao `MAJOR.MINOR.PATCH` valida. O dispositivo
confere hash e versao embutida antes de ativar. Use o script em vez de montar as
chamadas manualmente. O segredo nao e enviado em claro, mas HTTP
nao protege metadados/disponibilidade contra intermediarios; nunca exponha a
porta fora da LAN confiavel. O script acompanha o reboot e verifica que o slot
mudou e a nova imagem foi validada; se nao houver resposta conclusiva,
**nao reenvie automaticamente**.
Verifique estado/versao e diagnosticos antes de tentar outra vez. Uma imagem
nova so e confirmada no checkpoint de boot normal e apos Wi-Fi obter IP em
ate 30 s; falha de boot ou de rede nesse prazo solicita rollback do ESP-IDF.
`factory` e imagem de resgate, nao garantia de fallback
automatico em toda falha. Para validar pela primeira vez, teste cancelamento,
upload interrompido, hash incorreto, confirmacao, modo silencioso, perda de
energia e rollback controlado, registrando build, imagem, boot e efeito.

### Comandos pela LAN (/control)

Para validar pela LAN sem ocupar a USB Serial-JTAG (o `idf.py monitor` pode
ficar aberto), use o canal autenticado `/control/*` na porta 80. Provisione a
chave dedicada **uma vez**, com o monitor fechado, e depois trabalhe pela rede:

```bash
python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision
python3 tools/cyberdeck_http_cli.py --host <IP-DO-TAB5> type "help"
python3 tools/cyberdeck_http_cli.py --host <IP-DO-TAB5> terminal
```

A chave fica em `~/.config/cyberdeck5/control.key` (0600), é **independente da
chave OTA** e nunca deve ser impressa, commitada ou anexada a logs. O comando
`control.key` da ponte USB é o único caminho para lê-la. `type` tem o mesmo
alcance de `ui.type` (shell local, SSH, apps e diálogos) e responde
`202 Accepted` apenas para o despacho enfileirado — confirme o efeito com
`terminal` (ou visualmente) e use o monitor para panic/reboot. HTTP não oferece
confidencialidade nem identifica o usuário; use apenas em LAN confiável. Não
execute comandos destrutivos como prova. Roteiro completo em
`tests/manual/http-control-validation.pt-BR.md`.

- Ordem padrão: implementação -> testes focados -> validação no dispositivo
  conectado (quando segura e autorizada) -> revisão final ampla. Faça o flash do
  firmware compilado somente como parte da validação aprovada, não para substituir
  a inspeção/diagnóstico da implementação.
- Confirme no hardware os critérios de aceite pertinentes e registre build/hash,
  comandos e efeito observado. Não encaminhe a revisão final ampla antes desta
  etapa quando ela puder ser realizada com segurança; testes host/build não
  substituem o comportamento físico.
- Se o dispositivo estiver indisponível, o teste for inseguro ou houver bloqueio
  explícito/inconclusivo, documente a razão e mantenha a validação física pendente.
  O reviewer ainda pode concluir os gates locais finais, mas não aprova hardware
  nem permite marcar a tarefa como completa.
- Não repita uma interação física que causou reboot, panic ou comportamento
  perigoso antes de analisar evidências e obter autorização apropriada.
- Não faça flash automaticamente apenas para investigar um problema; o flash
  como etapa de validação da implementação é esperado quando o dispositivo
  estiver disponível.
- Números visíveis ao usuário (percentual, temperatura, RSSI, uptime) exigem
  série temporal, não amostra isolada: colete em resolução de 1 s por tempo
  suficiente para atravessar o transiente (partida, corte, reancoragem) e anexe
  o arquivo da série à evidência, indicando resolução, duração e falhas de
  leitura. Amostragem de 3 s ou mais esconde oscilação de 1 ponto.
- Prefira o endpoint HTTP que expõe a mesma grandeza (por exemplo
  `GET /battery`) quando ele existir: mede sem o cabo Serial-JTAG e sem
  competir pela porta serial.
- Registre no relatório qual coletor foi usado; se ele for reutilizável, mova-o
  para `tools/` em vez de manter script ad-hoc fora do repositório.

## Validacao no dispositivo via Serial-JTAG

O firmware possui uma ponte NDJSON na USB Serial-JTAG para testar o dispositivo
real sem depender de toque, teclado ou leitura visual manual. Antes de iniciar
uma validacao, leia o roteiro detalhado em
`tests/manual/serial-bridge-validation.pt-BR.md` e confirme que o firmware em
execucao corresponde ao commit que esta sendo testado.

### Pre-condicoes

- O dispositivo esta conectado e a porta foi identificada, normalmente
  `/dev/ttyACM0`.
- O firmware foi compilado e gravado quando necessario; nao execute `flash`
  automaticamente apenas para investigar um problema.
- `pyserial` esta instalado (`python3 -c 'import serial'`).
- A CLI responde sem erro (`python3 tools/cyberdeck_cli.py --help`).
- O monitor serial pode ser mantido em paralelo para capturar boot, logs,
  panic, reboot e mensagens intercaladas com as respostas NDJSON.

### CLI e comandos basicos

Use `--port` quando a porta nao for `/dev/ttyACM0`:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ping
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 sys.info
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 wifi.status
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 wifi.scan
```

Os comandos de UI sao executados no contexto seguro do firmware:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.dump
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.type "help"
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.type "wifi"
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.type "wifi audit"
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.clear
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.tap "wifi"
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 ui.click 100 100
```

`ui.type` injeta texto e Enter. Para testar entrada multilinha, use `\\n` no
argumento; a CLI deve preservar a ordem dos segmentos. Depois de ações que
alterem o terminal, como `ui.type`, prefira `term.dump` para validar o texto e
o retorno apresentado, pois a recuperação textual pela ponte é mais rápida e
objetiva que screenshot/OCR. Use `ui.dump` para estrutura, estado e geometria
dos widgets. A ponte usa
fila bounded para entrada, portanto não substitua a validação por um flood de
comandos sem observar possíveis perdas ou bloqueios.

### Dump e captura da tela

`screen.shot` retorna somente os metadados da captura atual. `screen.dump`
reconstroi e valida o BMP 24-bit, incluindo tamanho, continuidade dos chunks,
Base64 canônico e CRC32:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 screen.shot
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 screen.dump --out /tmp/cyberdeck-screen.bmp
file /tmp/cyberdeck-screen.bmp
```

Abra o BMP ou inspecione-o com uma ferramenta local para confirmar layout,
orientacao, texto e estado visual. O arquivo em `/tmp` e um artefato local de
validacao e nao deve ser incluido no `code-map.md`.

### Procedimento de diagnostico

1. Execute `sys.info` antes do cenario para registrar versao, heap e uptime.
2. Execute o comando ou injete a interacao com `ui.type`, `ui.tap` ou
   `ui.click`.
3. Após `ui.type` ou outra ação textual, use preferencialmente `term.dump` para
   validar texto/retorno. Capture `ui.dump` para estrutura/estado dos widgets.
   Reserve `screen.dump`/screenshot para validação visual, layout, cores,
   orientação ou quando o texto não for suficiente.
4. Execute `sys.info` novamente para verificar reboot, queda de uptime ou
   degradacao evidente de heap.
5. Registre a porta, identificacao do dispositivo, commit, horario, comandos,
   respostas JSON, logs do monitor e qualquer panic/reboot observado.

As respostas sao correlacionadas por `rid`. A CLI ignora logs e linhas que nao
sejam envelopes da requisicao atual e tolera leituras fragmentadas. Um erro
JSON com `ok:false` e falha do teste, mesmo que a sessao serial continue viva.
Ausencia de resposta, timeout, `Guru Meditation`, reboot ou queda inesperada de
uptime tambem deve ser reportada como falha do cenário.

Nao declare uma implementacao validada apenas porque `ui.type` foi aceito: a
validacao deve confirmar o efeito com `term.dump` (preferencialmente para
texto/retorno), `ui.dump` (estrutura/estado), `screen.dump`/screenshot (visual)
ou resposta especifica do comando/log correspondente. Para cobertura completa, execute o
roteiro manual, incluindo logs intercalados, scan Wi-Fi, auditoria, captura
repetida, rotacao e desconexao/reconexao USB. Testes host validam a camada pura,
mas nao substituem a validacao da secao `ESP_PLATFORM` no dispositivo.
