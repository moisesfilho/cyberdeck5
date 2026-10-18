# Help: requisitos, cenários e evidência host (REQ-HELP-01..04)

Matriz `REQ-HELP-01..04 -> TEST` do catálogo único de help usado pelo menu, pelo
shell local e pelo help por comando. As definições de REQ-HELP-01..04 abaixo são
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

Limites explícitos:

- Os cenários são evidência host do catálogo e do roteamento puro; não provam o
  help renderizado no LVGL, a largura real de linha no Tab5, nem o texto
  recuperado por `term.dump`.
- `help_unification_contract.py` é inspeção estrutural de fonte; não executa o
  firmware.
- A confirmação no dispositivo do help hierárquico/parâmetros é o item CMD1 de
  `tests/manual/tui-shell-validation.pt-BR.md`, que permanece **pendente** e não
  é alegado como validado por esta matriz.
- Nenhum teste desta matriz altera produção nem enfraquece o pino byte a byte:
  mudança intencional de texto exige atualizar o pino, nunca pular a comparação.
