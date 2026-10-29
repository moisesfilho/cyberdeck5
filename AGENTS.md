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

## Validacao de firmware no dispositivo

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
