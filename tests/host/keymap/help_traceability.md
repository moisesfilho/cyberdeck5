# Help: requisitos, cenários e evidência host (REQ-HELP-01..05)

Matriz `REQ-HELP-01..05 -> TEST` do catálogo único de help usado pelo menu, pelo
shell local e pelo help por comando. As definições de REQ-HELP-01..05 abaixo são
as registradas nos contratos dos próprios testes (não há documento de requisitos
externo a este repositório); se o texto de um requisito mudar, o contrato e esta
matriz devem mudar juntos, nunca a asserção.

Todos os cenários ligam os TUs reais de produção
(`cyberdeck_shell_utils.cpp`, `cyberdeck_shell_help.h` e
`cyberdeck_local_shell.cpp`) no host, sem LVGL/FreeRTOS/hardware. Nenhuma linha
abaixo é validação física do help no Tab5.

| REQ aprovado | TEST e cobertura | Evidência/resultado focado |
| --- | --- | --- |
| REQ-HELP-01: cada comando legado renderiza hierarquia de duas linhas — cabeçalho `<comando> [terminal]` na coluna 0 e, imediatamente abaixo, a linha de uso indentada com exatamente dois espaços, iniciando pelo mesmo comando do cabeçalho. Comandos dinâmicos `[app]` seguem o mesmo contrato hierárquico: cabeçalho `<comando> [app]` na coluna 0 e descrição indentada na linha seguinte | TEST-HELP-HIERARCHY-01 (`test_shell_session.cpp::main`, bloco `TEST-HELP-CAT-PINNED / TEST-HELP-HIERARCHY-01`): exige `ls [terminal]\n  ls [-a] [path] - list directory contents\n`, `cat [terminal]\n  cat <file> - print a regular file\n` e `help [terminal]\n  help - show this help\n` na saída real da sessão. TEST-HELP-HIERARCHY-02 (`test_help_unification.cpp::test_catalog_is_complete_and_unique`): verificação estrutural independente que deriva o comando do cabeçalho em coluna 0 e exige que a linha de uso repita o mesmo comando antes de ` - `; TEST-HELP-GOLDEN (`test_shell_utils.cpp::test_help_text_structure`) conta 18 tags e 47 linhas indentadas. TEST-HELP-APP-HIERARCHY-01 (`test_help_unification.cpp::test_app_entry_format_and_size`): `format_app_entry("editor-tool")` renderiza exatamente `"editor-tool [app]\n  start compiled application\n"`; TEST-HELP-APP-HIERARCHY-02 (`test_help_unification.cpp::test_app_entry_hierarchy_contract`): verificação estrutural independente do bloco hierárquico — header coluna 0, descrição indentada, tag `[app]` presente e `[terminal]` ausente; ausência/colisão e saída dinâmica integral derivadas do catálogo de produção via `command_text()` e de um golden literal, nunca do vetor local | PASS: `test_shell_session` 342 checks e `test_help_unification` 623 checks; `test_shell_utils` 477 checks. Não há auto-comparação de substring da própria linha: cabeçalho duplicado, reordenado ou mal indentado falha mesmo se o golden fosse regerado. |
| REQ-HELP-02: parâmetros são explicados em linhas próprias, exatamente um nível de indentação, com separador `:` e explicação não vazia; a descrição após ` - ` é não vazia e sem espaços nas bordas | TEST-HELP-HIERARCHY-02 (`test_help_unification.cpp::test_catalog_is_complete_and_unique`): por bloco, exige `:` em cada linha de parâmetro, conteúdo não vazio, sem espaço extra de indentação e sem trailing space; exige descrição não vazia/trimada. TEST-HELP-CAT-PINNED (`test_shell_session.cpp::kLegacyHelpPinned` + `test_help_unification.cpp::test_catalog_is_complete_and_unique`): pino byte a byte com todas as explicações de parâmetro. TEST-HELP-CMD-01 (`test_shell_session.cpp::main`): `poweroff -h/--help` e `ls -h` entregam o bloco pinado com a explicação de confirmação | PASS: igualdade byte a byte contra o pino e verificação estrutural independente. `test_ble_command_parse` 209 checks cobre a linha `bluetooth [search|paired]` e a posição da descrição com guard de limite antes do índice. |
| REQ-HELP-03: ordem canônica, um bloco por comando, sem linha em branco nem linha ragged, terminando em um único newline (18 tags + 47 indentadas = 65 linhas). Blocos dinâmicos `[app]` seguem o mesmo formato hierárquico de duas linhas, sem linha em branco entre blocos | TEST-HELP-ORDER-01 e TEST-HELP-ROWS-01 (`test_shell_session.cpp::main`): percorre `kLegacyOrder` exigindo cada cabeçalho `[terminal]` na ordem, ausência de `\n\n`, newline final único e toda linha indentada com exatamente dois espaços seguidos de conteúdo. TEST-HELP-GOLDEN (`test_shell_utils.cpp::test_help_text_structure`): 65 newlines, 65 linhas não vazias, 18 tags, 47 indentadas. TEST-HELP-APP-HIERARCHY-01/02: comparação integral da saída dinâmica — `expected_output` inclui os blocos hierárquicos `edit [app]\n  start compiled application\n`, `demo [app]\n  start compiled application\n`, `demo-tool [app]\n  start compiled application\n` | PASS: 65 linhas e ordem canônica verificadas em `test_shell_utils` 477 checks e `test_shell_session` 342 checks. `help_unification_contract.py` (estrutural) rejeita listas literais independentes nas rotas de shell/UI. |
| REQ-HELP-04: catálogo único compartilhado — `help`, `help -h`, `help --help` e o help por comando (`-h`/`--help`) rendem o mesmo catálogo/bloco, e cada usage e cada cabeçalho aparece exatamente uma vez. Blocos dinâmicos `[app]` hierárquicos são adicionados ao fim do catálogo legado, sem duplicar comandos legados | TEST-HELP-PARITY (`test_help_unification.cpp::test_help_parity`): as três rotas de `help` rendem o mesmo texto. TEST-HELP-CAT-PINNED (`test_shell_session.cpp::main`): a saída da sessão após o eco é igual ao catálogo do formatter e igual ao pino, impedindo uma segunda lista. TEST-HELP-CMD-BLOCKS (`test_help_unification.cpp::test_local_command_help_uses_catalog_lines`): cada comando local `-h/--help` imprime seu bloco do catálogo. TEST-HELP-CMD-01: `help -h`, `help --help`, `poweroff -h/--help` e `ls -h` na sessão; `help wifi` falha fechado com `usage: help`. Unicidade de usage/cabeçalho em TEST-HELP-HIERARCHY-02. TEST-HELP-APP-HIERARCHY-01/02: `format_app_entry` renderiza bloco hierárquico de duas linhas; ausência/colisão e saída dinâmica integral verificadas contra o catálogo de produção (`command_text`) e contra um golden literal, sem colisão legada (`ls`) nem duplicata (`demo-tool`) | PASS: paridade e pino byte a byte; `help_unification_contract.py` verifica fonte canônica, fórmula/reserve de tamanho e ausência de listas literais independentes. |
| REQ-HELP-05: o help de um app comandado revela TODAS as linhas de uso/sobcomandos declaradas no manifesto do app (editor: `edit <path>`, `edit save`, `edit save as <path>`, `edit save as confirm`; netdiag: todos os subcomandos; demo: nota fiel de que argumentos extras são ignorados), e o inventário compilado tem exatamente 11 apps (3 comandados + 7 serviços sem comando + shell excluído por `owns_console`), sem efeitos colaterais no iniciar/executar | TEST-HELP-APP-USAGE-EDIT (`test_shell_help.cpp::main`): golden literal independente do manifesto do editor exige as 4 linhas de uso e as explicações. TEST-HELP-APP-USAGE-NETDIAG: manifesto literal do netdiag — cada subcomando declarado aparece na saída real. TEST-HELP-APP-USAGE-DEMO: usa o `demo_application` REAL de produção — `usage: demo` sem argumentos declarados + nota de extras ignorados. TEST-HELP-APP-INVENTORY: 11 apps registrados (ids em ordem), 3 comandados (`cyberdeck.demo|editor|netdiag`), 7 serviços, 1 console owner. TEST-HELP-APP-ALIAS: alias de comando resolve o mesmo help. TEST-HELP-APP-COLLISION: comando legado não colide com comando de app. TEST-HELP-APP-NO-EXEC: `check_no_side_effects` — help não toca a porta de plataforma (start/execute). TEST-HELP-APP-SYNTAX: regra real do handler — cair para o dispatcher com demanda inicia o app foreground. **Contrato estrutural (ciclo 5) — TEST-HELP-APP-MANIFEST-01..05 + TEST-HELP-WIRING-01 (`test_shell_help_contract.py`)**: inspeção determinística de fonte que lê o `k_apps` real de `cyberdeck_system_apps.cpp` e os manifestos reais de shell/demo/editor/netdiag, exigindo 11 apps na ordem de registro, 3 comandados (`cyberdeck.demo|editor|netdiag`), 7 serviços sem `commands` (10 argumentos em `make_manifest`), 1 console owner, e para cada app comandado a lista exata de `command_help_entries` aprovada; além disso reconcilia os espelhos de `test_shell_help.cpp` (fixtures `commandless_manifest(...)`, `editor_usage_manifest()`, `netdiag_usage_manifest()` e o uso do `demo_application` real) contra os mesmos manifestos de produção e confirma que o golden contém cada linha `usage:` e a primeira linha de cada explicação declarada | PASS: `test_shell_help` 244 checks, 0 failures (REQ-HELP-05 completo) e `test_shell_help_contract.py` PASS nos 6 cenários. Nenhuma asserção foi enfraquecida: o pino byte a byte das 4 linhas do editor, do bloco netdiag, do bloco real do demo e do inventário segue intacto. |

## Contagens e limites desta matriz

Medição do snapshot atual, executando somente os alvos focados
(`make -C tests/host/keymap test_shell_utils test_help_unification
test_ble_command_parse`): `test_shell_utils` 477 checks, `test_help_unification`
623 checks e `test_ble_command_parse` 209 checks, todos PASS. O
`test_shell_session`, que ancora TEST-HELP-HIERARCHY-01/ORDER-01/ROWS-01/CMD-01,
reporta 342 checks no snapshot atual. Essas são as contagens que a linha B3 de
`wifi_survey_traceability.md` registra (as três primeiras).

Ciclo 2 — formatter dinâmico `[app]` hierárquico: os testes
TEST-HELP-APP-HIERARCHY-01 (`test_app_entry_format_and_size`) e
TEST-HELP-APP-HIERARCHY-02 (`test_app_entry_hierarchy_contract`) foram
adicionados em `test_help_unification.cpp`. O bloco
TEST-HELP-LEGACY-PARITY/RUNTIME-MANIFESTS em `test_shell_session.cpp` foi
atualizado para exigir o formato hierárquico (`edit [app]\n  start compiled
application\n`, etc.) na comparação integral da saída dinâmica. Os testes
REQ-HELP-01..04 permanecem inalterados em seus contratos originais.

Ciclo 3 — remoção de asserções tautológicas em
TEST-HELP-APP-HIERARCHY-02 (`test_app_entry_hierarchy_contract`): o bloco
"Absent" comparava o vetor local `commands` contra si mesmo e o bloco
"Full dynamic output" comparava `format_app_entry` contra si mesmo; ambos
foram substituídos por verificações independentes contra o catálogo de
produção e a saída de runtime. A ausência/colisão usa o lookup por comando
real (`cyberdeck_shell_help::command_text`) — `ls` precisa resolver como
comando legado e `no-command` não pode resolver — e a saída dinâmica integral
é comparada a um golden literal escrito à mão, sem auto-comparação do
formatter. Os contratos REQ-HELP-01..04 não foram enfraquecidos nem
removidos. Contagem remanescente: `test_help_unification` 623 checks.

Ciclo 4 — REQ-HELP-05 (`make -C tests/host/keymap test_shell_help` e
`./test_shell_help`): 244 checks, 0 failures. A produção
(`app_command_help()` em `cyberdeck_shell_session.cpp`) passou a revelar todas
as entradas declaradas do comando primário (`command_help_entries` inteiro)
e apenas a entrada do alias para comandos não primários, então `edit help`
revela `edit <path>`, `edit save`, `edit save as <path>` e
`edit save as confirm` com suas explicações. O alvo `test_shell_help` no
Makefile linka `$(DEMO_APP_SRC) $(DEMO_APP_HDR)` para exercitar o manifesto
real do demo. O blocker registrado nos ciclos anteriores foi encerrado pela
correção da produção; a asserção nunca foi enfraquecida.

Ciclo 5 — reconciliação estrutural dos manifestos reais
(`test_shell_help_contract.py`, alvo `test_shell_help_contract`): o binário
comportamental ainda precisa registrar editor e netdiag com fixtures próprias,
porque essas duas aplicações de plataforma não são linkáveis no host. O
contrato fecha essa lacuna lendo os dois lados como fonte:

- TEST-HELP-APP-MANIFEST-01: ordem real de `k_apps`, ids, nomes, descrições,
  tipos, `owns_console` e as contagens derivadas (3 comandados, 7 serviços,
  1 console owner). A ordem posicional de `struct manifest` é pinada antes de
  qualquer índice, para que um header reordenado falhe em vez de ler o campo
  errado.
- TEST-HELP-APP-MANIFEST-02: para cada app comandado, a lista exata de
  `command_help_entries` declarada (editor 4, netdiag 1, demo 1), sem entradas
  extras que o handler do comando primário renderizaria, e com metadata de uso
  não vazia em cada comando declarado.
- TEST-HELP-APP-MANIFEST-03: os 7 serviços chamam `make_manifest` com 10
  argumentos (sem lista de comandos) e não declaram `command`.
- TEST-HELP-APP-MANIFEST-04: os espelhos de `test_shell_help.cpp` são
  comparados com os manifestos reais — fixtures de serviço/shell (nome,
  descrição, tipo), `owns_console`, versão do helper, uso do `demo_application`
  real, ordem dos 3 comandados e as fixtures de uso do editor/netdiag
  (id, nome, descrição, versão, tipo, comando, lista de comandos e cada par
  uso/explicação). Um espelho que divergir da produção falha aqui.
- TEST-HELP-APP-MANIFEST-05: o golden comportamental permanece literal e
  independente do formatador; o contrato apenas confirma que cada linha
  `usage:` declarada e a primeira linha de cada explicação estão presentes no
  golden.
- TEST-HELP-WIRING-01: `test_shell_help` linka `$(DEMO_APP_SRC)`,
  `test_shell_help_contract` roda o contrato Python e ambos são agregados ao
  alvo `test` e declarados `.PHONY`.

O contrato é inspeção estrutural de fonte: não executa o firmware, não abre
display e não substitui a evidência comportamental (`test_shell_help`) nem a
validação física.

Limites explícitos:

- Os cenários são evidência host do catálogo e do roteamento puro; não provam o
  help renderizado no LVGL, a largura real de linha no Tab5, nem o texto
  recuperado por `term.dump`.
- `help_unification_contract.py` e `test_shell_help_contract.py` são inspeção
  estrutural de fonte; nenhum dos dois executa o firmware. O contrato lê
  instruções/contratos de produção por referência (parsing de `k_apps`,
  `struct manifest` e dos agregados posicionais), o que é uma limitação: um
  construtor não literal de manifesto (por exemplo um `make_manifest` dinâmico
  para um app comandado) não seria reconciliado automaticamente e exigiria
  estender o parser antes de alegar cobertura.
- A reconciliação cobre os espelhos que hoje existem (7 serviços, shell, editor
  e netdiag). A ordem dos 8 primeiros registros da fixture não é comparada
  símbolo a símbolo; as garantias ali são as contagens (11 apps, 7 serviços, 1
  console owner) e o casamento de id/nome/descrição/tipo de cada serviço.
- A confirmação no dispositivo do help hierárquico/parâmetros é o item CMD1 de
  `tests/manual/tui-shell-validation.pt-BR.md`, que permanece **pendente** e não
  é alegado como validado por esta matriz.
- Nenhum teste desta matriz altera produção nem enfraquece o pino byte a byte:
  mudança intencional de texto exige atualizar o pino, nunca pular a comparação.
