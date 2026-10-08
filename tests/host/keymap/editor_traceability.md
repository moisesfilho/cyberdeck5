# Editor de texto (`edit`) — contrato e rastreabilidade

Escopo: `REQ-EDIT-01..19` / `AC-EDIT-01..19`. A matriz abaixo liga cada
requisito ao cenário host que o verifica. Os testes host não acessam VFS, SD,
LVGL, rede ou hardware; o contrato de runtime é inspeção estrutural e o
modelo é executado com dados determinísticos.

## Contratos

| ID | Requisito | Critério de aceitação (AC) |
|---|---|---|
| REQ-EDIT-01 | App foreground compilada | `cyberdeck.editor` declara dependência, grants SDK, lifecycle e comando `edit`; start/stop são idempotentes. |
| REQ-EDIT-02 | Leitura confinada ao SDK | A abertura usa somente `storage_facade::bounded_read`; o app não inclui VFS/descritor nem acessa o backend diretamente. |
| REQ-EDIT-03 | Limite de documento e caminho | Documentos aceitam no máximo 12000 bytes; payload vazio ou caminho ausente/maior que 256 bytes é rejeitado sem leitura indevida. |
| REQ-EDIT-04 | Codecs e EOL | UTF-8, UTF-16 LE/BE e Windows-1252 válidos decodificam; arquivos existentes preservam codec, BOM e EOL, e novos arquivos usam UTF-8 sem BOM/LF. |
| REQ-EDIT-05 | Rejeição fail-closed | Binário com NUL, UTF-16 ímpar, bytes inválidos, codec não suportado, arquivo oversized e erro de leitura não-`ENOENT` não abrem nem mutam o documento. |
| REQ-EDIT-06 | Modelo de edição | Inserção, substituição, busca, backspace/delete, Enter, navegação direcional/gestual e cursor operam em fronteiras UTF-8; undo/redo permanecem bounded. |
| REQ-EDIT-07 | Save-as confirmado | `save as <arquivo>` somente arma a confirmação; apenas confirmação explícita define o destino e prossegue para save. |
| REQ-EDIT-08 | Save atômico | Save escreve temporário, executa flush/fsync e rename atômico; qualquer falha rejeita, preserva dirty e não publica sucesso. |
| REQ-EDIT-09 | Estado e lifecycle seguro | Stop limpa documento e save-as pendente; dirty só é limpo depois do rename atômico bem-sucedido. |
| REQ-EDIT-10 | Entrada encaminhada | Enquanto running, cada tecla é encaminhada ao modelo; parada ou entrada rejeitada não altera o documento nem permite edição fora do lifecycle. |
| REQ-EDIT-11 | Contrato documentado e rastreável | Documentação, mapa e plano apontam para os arquivos do editor e para esta matriz, sem alegar cobertura manual/hardware inexistente. |
| REQ-EDIT-12 | Superfície LVGL bounded | A view usa slots fixos, linhas limitadas/clipadas e não acessa VFS; renderização não cresce sem limite. |
| REQ-EDIT-13 | Cursor por codepoint | Edição e marcador respeitam fronteiras UTF-8; cursor fora do documento é clampado antes do render. |
| REQ-EDIT-14 | Scroll e gesto | Gesto vertical altera somente a janela bounded e retorna uma ação para repintura. |
| REQ-EDIT-15 | Entrada, atalhos e diálogos | Texto/setas/delete/enter, Ctrl+F Buscar, Ctrl+S Salvar, Ctrl+Q Fechar, Ctrl+Z/Y e Salvar/Descartar/Cancelar são roteados pela app. |
| REQ-EDIT-16 | Arquivo novo | ENOENT abre documento vazio, novo save usa UTF-8 sem BOM e LF. |
| REQ-EDIT-17 | Falha atômica | write-temp, flush/fsync e rename são ordenados; falha rejeita e preserva dirty/estado publicado. |
| REQ-EDIT-18 | Lifecycle, grants e Serial | Contexto/grant/callbacks têm ownership simétrico, teardown invalida recursos e contratos entram no Makefile host. |
| REQ-EDIT-19 | Rodapé bounded e encoding | O editor renderiza uma única linha de rodapé com `Ctrl+F Buscar`, `Ctrl+S Salvar` e `Ctrl+Q Fechar`, e somente `UTF-8`, `UTF-16LE`, `UTF-16BE` ou `Windows-1252`; texto acima de 256 bytes é truncado sem perder a sigla, e inset, orientação, cursor, scroll, gesto e lifecycle não produzem overlap. |

## Matriz REQ/AC → TEST

| Requisito/AC | Teste(s) | Cenários cobertos |
|---|---|---|
| REQ-EDIT-01 / AC-EDIT-01 | TEST-EDIT-RUNTIME | positivo: manifesto, grants, dependência, comando e start; limite: lifecycle repetido; falha: comando parado/rejeitado. |
| REQ-EDIT-02 / AC-EDIT-02 | TEST-EDIT-VFS | positivo: bounded read via SDK; negativo: include de VFS, descritor, `O_RDONLY` ou acesso direto ao backend. |
| REQ-EDIT-03 / AC-EDIT-03 | TEST-EDIT-MODEL, TEST-EDIT-RUNTIME | limite: 0, 11999, 12000 e 12001 bytes; oversize rejeitado antes de substituir documento aberto; negativo: caminho vazio e path acima de 256 bytes. |
| REQ-EDIT-04 / AC-EDIT-04 | TEST-EDIT-CODEC | positivo: quatro codecs/round-trip; limite: BOM, CRLF/CR/LF, CR isolado e documento vazio. |
| REQ-EDIT-05 / AC-EDIT-05 | TEST-EDIT-CODEC, TEST-EDIT-VFS | negativo/falha: NUL, UTF-16 ímpar, encoding inválido, binary/oversized e erro de leitura; nenhum estado parcial. |
| REQ-EDIT-06 / AC-EDIT-06 | TEST-EDIT-MODEL, TEST-EDIT-INPUT | positivo: busca, replace, edição, undo; limite: codepoint, cursor, vertical/gesto e undo bounded; negativo: operação não aplicável. |
| REQ-EDIT-07 / AC-EDIT-07 | TEST-EDIT-SAVE, TEST-EDIT-INPUT | positivo: pedido + confirmação; trim de destino; destino só muda após rename; negativo: sem confirmação, destino vazio e confirmação sem pending save-as. |
| REQ-EDIT-08 / AC-EDIT-08 | TEST-EDIT-SAVE, TEST-EDIT-STORAGE, TEST-EDIT-STORAGE-REAL, TEST-EDIT-VFS | host comportamental com fake backend e backend real temporário/confinado: write-temp → flush/fsync-file → rename; fsync do diretório após rename; resultado estruturado preserva stage/errno; falha em cada etapa rejeita e não limpa dirty. |
| REQ-EDIT-09 / AC-EDIT-09 | TEST-EDIT-RUNTIME, TEST-EDIT-SAVE | limite/regressão: stop após edição e restart; falha: save sem path/encode/commit não publica sucesso. |
| REQ-EDIT-10 / AC-EDIT-10 | TEST-EDIT-INPUT | positivo: bind/validação do input e tecla chega ao modelo; negativo: app parada; limite: entrada inválida/sem mutação. |
| REQ-EDIT-11 / AC-EDIT-11 | TEST-EDIT-DOC | positivo: referências e caminhos existentes em README, arquitetura, plano, code-map e esta matriz; negativo: cobertura manual/hardware não é alegada. |
| REQ-EDIT-12 / AC-EDIT-12 | TEST-EDIT-UI | positivo: 64 slots bounded e linhas clipadas; limite: largura 256; negativo: view sem VFS/storage. |
| REQ-EDIT-13 / AC-EDIT-13 | TEST-EDIT-INPUT | positivo: next/previous codepoint e cursor clampado; limite: marcador em UTF-8. |
| REQ-EDIT-14 / AC-EDIT-14 | TEST-EDIT-UI | positivo: gesto produz scroll e repintura bounded. |
| REQ-EDIT-15 / AC-EDIT-15 | TEST-EDIT-INPUT, TEST-EDIT-COMMANDS, TEST-EDIT-SAVE-FAILURE | positivo: Ctrl+S; Ctrl+Q+Enter fecha somente após sucesso; D descarta; limite: busca; falha: diálogo dirty permanece e diagnóstico é exibido. |
| REQ-EDIT-16 / AC-EDIT-16 | TEST-EDIT-NEW, TEST-EDIT-CODEC | positivo: ENOENT e UTF-8/LF sem BOM; limite: vazio. |
| REQ-EDIT-17 / AC-EDIT-17 | TEST-EDIT-SAVE-FAILURE, TEST-EDIT-STORAGE, TEST-EDIT-STORAGE-REAL | host comportamental: lambdas falham em write, flush/fsync ou rename com stage/errno; backend real temporário cobre destino ausente/existente, backup `.rollback` sem overwrite, resultado primary/rollback stage/errno/attempted, sucesso, original e sidecars; publicação inválida e grant revogado; original, dirty e diálogo permanecem; regressão: dirty só limpa após publicação e orphan `.tmp` não é publicado. |
| REQ-EDIT-18 / AC-EDIT-18 | TEST-EDIT-LIFECYCLE, TEST-EDIT-SERIAL | positivo: bind/refresh/validate; falha: unbind/invalidação/destroy; integração: Makefile. |
| REQ-EDIT-19 / AC-EDIT-19 | TEST-EDIT-FOOTER-HOST, TEST-EDIT-FOOTER-CONTRACT, TEST-EDIT-FOOTER-SERIAL | positivo: uma linha bounded com `Ctrl+F Buscar`, `Ctrl+S Salvar`, `Ctrl+Q Fechar` e as quatro siglas; limite: truncamento preserva a sigla, teclado virtual/inset, retrato/paisagem e resize; regressão: cursor/scroll, toque/gesto, teardown e ausência de overlap. |
| TEST-EDIT-FOOTER-HOST/CONTRACT/SERIAL | `test_display_views.cpp`, `test_editor_surface_contract.py` | uma linha bounded com encoding e atalhos; altura dinâmica; inset de teclado virtual; retrato/paisagem/resize; `k_max_lines` bounded; cursor/scroll clampados; toque/gesto e teardown preservados. |

## Catálogo dos testes e evidência

| ID | Fonte/cenário formal | Evidência esperada |
|---|---|---|
| TEST-EDIT-RUNTIME | `test_editor_runtime_contract.py`: manifesto, lifecycle, comando, limite, stop e falhas de execução | host (PASS confirmado pelo tester) |
| TEST-EDIT-VFS | `test_editor_runtime_contract.py`: storage SDK e ausência de VFS/descritor/backend direto | host (PASS confirmado pelo tester) |
| TEST-EDIT-CODEC | `test_editor_model.cpp::codec_boundaries`, `encoding_round_trip`: codecs válidos, BOM/EOL, CR isolado e rejeições | host (PASS confirmado pelo tester) |
| TEST-EDIT-MODEL | `test_editor_model.cpp::codec_boundaries`, `editing_and_history`, `oversized_open_is_rejected_before_mutation`: limites, oversize sem mutação, busca, edição, cursor, undo/redo e gesto | host (PASS confirmado pelo tester) |
| TEST-EDIT-SAVE | `test_editor_model.cpp::editing_and_history` e `test_editor_runtime_contract.py`: trim, confirmação, sequência atômica, publicação tardia do destino, dirty e falhas | host (PASS confirmado pelo tester) |
| TEST-EDIT-INPUT | `test_editor_model.cpp::editing_and_history` e `test_editor_runtime_contract.py`: bind/validação, teclas, entrada parada e roteamento | host (PASS confirmado pelo tester) |
| TEST-EDIT-DOC | `test_editor_runtime_contract.py`: referências documentais e rastreabilidade | host (PASS confirmado pelo tester) |
| TEST-EDIT-UI | `test_editor_surface_contract.py`: slots, clip, superfície sem VFS e gesto | host (contrato estrutural) |
| TEST-EDIT-COMMANDS | `test_editor_surface_contract.py`: Ctrl+F/S/Q/Z/Y e roteamento de teclas | host (contrato estrutural) |
| TEST-EDIT-DIRTY | `test_editor_surface_contract.py`: dirty, confirmação e publicação tardia | host (contrato estrutural) |
| TEST-EDIT-NEW | `test_editor_surface_contract.py` + `test_editor_model.cpp`: ENOENT e UTF-8/LF | host (PASS confirmado pelo tester) |
| TEST-EDIT-SAVE-FAILURE | `test_editor_surface_contract.py`: falhas das três etapas atômicas | host (contrato estrutural) |
| TEST-EDIT-STORAGE | `test_editor_save_contract.py`: fake backend comportamental, sucesso, falha injetada por lambda nos três estágios, errno/stage, original/dirty/dialog, orphan `.tmp` e fsync_directory best-effort; estrutura exige campos e estados de rollback e reserva exclusiva `.rollback` | host (comportamental + contrato estrutural) |
| TEST-EDIT-STORAGE-REAL | `test_storage_facade.cpp`: linka `cyberdeck_app_storage.cpp` e `cyberdeck_local_shell.cpp`; seam de root temporário, transação real, destino ausente/existente, sucesso e colisão `.rollback` preservando backup/original/temp, leitura e grant revogado | host (backend temporário confinado, gcov) |
| TEST-EDIT-LIFECYCLE | `test_editor_surface_contract.py` + `test_ui_resource_contract.py`: grants, teardown e contextos | host (contrato estrutural) |
| TEST-EDIT-SERIAL | `test_editor_surface_contract.py`: integração do contrato no Makefile host | host (contrato estrutural) |
| TEST-EDIT-FOOTER-HOST | `test_display_views.cpp`: uma linha com Ctrl+F/S/Q, quatro siglas, truncamento bounded, geometria, inset, resize, limites, cursor, scroll, toque e teardown | host (LVGL shim) |
| TEST-EDIT-FOOTER-CONTRACT | `test_editor_surface_contract.py`: uma linha com Ctrl+F/S/Q, limite 256, quatro siglas, layout dinâmico, inset, resize e clamps | host (contrato estrutural) |
| TEST-EDIT-FOOTER-SERIAL | `test_editor_surface_contract.py`: wiring de eventos e integração host; Serial real permanece manual | host (sem alegação de hardware) |

Nenhum cenário manual de dispositivo é reivindicado por esta matriz. Os casos
de falha de I/O e VFS são contratos estruturais; a execução real no firmware
permanece fora do gate host.
