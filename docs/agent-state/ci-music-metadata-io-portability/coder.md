# coder - ci-music-metadata-io-portability

- Role: coder
- Updated: 2026-11-08 00:19 (ciclo 1: correção de portabilidade do fixture)
- Code state: `develop` HEAD `467f283dce70c79b26e1d0d0bb8203e3136afe66` + worktree
  sujo com **um** arquivo modificado (nada untracked, nada commitado):
  - `git diff | sha256sum` = `7dc4df38af6ac2829aeb12d1eb1144946491f9ef72255c037f9e03fcd86e20e1`;
  - `git status --porcelain | sha256sum` =
    `9737a13535d79fd7a2abd327f1fb70797861f1d9aa1b9cce8aba86cdf07f213b`
    (1 entrada: `M tests/host/keymap/test_music_metadata_io.cpp`);
  - `tests/host/keymap/test_music_metadata_io.cpp` sha256 (16 hex)
    `495b5e097f296ac7`; `git diff --stat` = 1 arquivo, 8 inserções / 2 remoções.

## Dispatch objective

CI remoto do commit `467f283` falhou no step `Host aggregate and coverage`
(run id `42441766510`): `test_music_metadata_io` saía com
`FAIL: cannot create the temp directory` porque o fixture usava caminho fixo
`/tmp/opencode/music-metadata-io-XXXXXX` e o runner não cria `/tmp/opencode`
(diretório pré-criado só nesta máquina local).

Critério de saída: corrigir **somente** a portabilidade do fixture (base do
`mkdtemp` por `TMPDIR` com fallback `/tmp`), preservando unicidade do `mkdtemp`,
limpeza e todos os asserts; rodar o teste focado e provar com `TMPDIR` apontando
para um diretório temporário existente **sem** subdiretório `opencode`; sem
suíte, build, flash ou commit. Validação independente fica com o tester.

## Work done

Causa confirmada no código, não deduzida: em `test_music_metadata_io.cpp` a
função `make_temp_dir()` montava o pattern com
`std::snprintf(pattern.data(), …, "/tmp/opencode/music-metadata-io-XXXXXX")`
dentro de um `std::array<char, 64>`. `mkdtemp` não cria diretórios
intermediários, então o alvo falhava em qualquer host sem `/tmp/opencode` —
exatamente a linha 378 apontada no log do CI.

Alteração (única função tocada, mais um helper):

- novo `temp_dir_base()` (namespace anônimo do próprio teste): devolve
  `std::getenv("TMPDIR")` quando definido e **não vazio**, senão `"/tmp"`;
- `make_temp_dir()` passa a compor `std::string pattern =
  std::string(temp_dir_base()) + "/music-metadata-io-XXXXXX"` e chama
  `::mkdtemp(pattern.data())` (buffer mutável de `std::string` em C++17).

Preservado sem mudança de semântica: `mkdtemp` continua sendo a única fonte de
unicidade (`XXXXXX`), o retorno vazio em falha continua levando ao mesmo
`std::cerr << "FAIL: cannot create the temp directory"` em `main()`, a limpeza
segue sendo o `rm -rf '<dir>'` sobre o caminho **retornado** por `mkdtemp` (logo
acompanha automaticamente a base escolhida) e nenhum assert, fixture, wrapper
`--wrap=…` ou override de `operator new` foi tocado. As 205 asserções são as
mesmas de antes (205/205 antes e depois).

Deliberadamente **não** alterei o `Makefile`, o contrato estrutural, produção ou
qualquer documentação (ver Decisões).

## Evidence

Todos os comandos na raiz do repositório, sem `clean`, sem suíte, sem build IDF,
sem flash, sem commit. `TMPDIR` do shell local está **não definido**
(`TMPDIR=[<unset>]`), então o caminho de fallback `/tmp` é o exercitado por
padrão aqui.

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_music_metadata_io` (estado final, após a correção) | `205/205 checks passed`, exit 0; recompilou `test_music_metadata_io_bin` |
| `BASE=$(mktemp -d)` → `/tmp/tmp.glzFO3lrhO` (existe, writable, **sem** subdir `opencode`); `TMPDIR="$BASE" ./test_music_metadata_io_bin` | `205/205 checks passed`, exit 0 |
| mesma execução: `stat -c %y "$BASE"` antes/depois | `00:17:50.547` → `00:17:51.660` (mtime da base mudou) ⇒ o `mkdtemp` criou um subdiretório **dentro da base do `TMPDIR`**, não em `/tmp/opencode` |
| mesma execução: `ls -A "$BASE"` antes/depois | `[]` → `[]` e `rmdir` da base OK ⇒ a limpeza removeu o diretório e não deixou resíduo |
| controle negativo: `TMPDIR=/tmp/definitely-missing-base-945020` (confirmado inexistente) `./test_music_metadata_io_bin` | `FAIL: cannot create the temp directory`, exit 1 ⇒ o caminho agora é derivado de `TMPDIR` (com o hardcode anterior esse caso teria passado, pois `/tmp/opencode` existe nesta máquina) |
| `env -u TMPDIR ./test_music_metadata_io_bin` | `205/205 checks passed`, exit 0 ⇒ fallback `/tmp` funciona sem `TMPDIR` |
| `make -C tests/host/keymap CXXFLAGS='-std=c++17 -Wall -Wextra -Werror -O0 --coverage' test_music_metadata_io` | `205/205 checks passed`, exit 0 ⇒ compila/passa com as **flags exatas** do step de CI (`-Werror` + `-O0 --coverage`, conforme `.github/workflows/quality-gate.yml:130-131`) |
| artefato restaurado: `rm -f test_music_metadata_io_bin test_music_metadata_io_bin-*.gcda` + `make -C tests/host/keymap test_music_metadata_io` | `205/205 checks passed`; binário final recompilado com as flags **default** (`-O2`), para não deixar artefato instrumentado que o `make` incremental não recompilaria |
| `git diff --check` | limpo (exit 0) |
| `git rev-parse HEAD`, `git diff \| sha256sum`, `git status --porcelain \| sha256sum` | `467f283d…afe66`; `7dc4df38…6e20e1`; `9737a135…f213b` (1 entrada `M`) |

Não executados (por escopo explícito do despacho): agregado
`make -C tests/host/keymap test`/`clean`, cobertura/gcovr, `verify`,
ASan/UBSan/TSan, `test_music_id3_contract` e demais alvos irmãos, `idf.py build`,
flash, commit/push. Nada aqui deve ser lido como suíte ou CI verdes.

## Decisions and assumptions

- **`TMPDIR` como base, `/tmp` como fallback**: é a convenção já usada nos
  contratos Python deste repositório
  (`test_wifi_audit_contract.py::_scratch_root`, `test_screen_protection_contract.py`:
  `TMPDIR` → `/tmp/opencode` → `tempfile.gettempdir()`), mas sem o passo
  intermediário `/tmp/opencode`, que é justamente a dependência local que o
  despacho mandou remover.
- **`TMPDIR` definido mas inválido falha em vez de mascarar**: não tento criar a
  base nem cair silenciosamente para `/tmp`; ausência/permissão geram o mesmo
  `FAIL: cannot create the temp directory`, agora apontando misconfiguração real.
  É o controle negativo que sustenta a prova de que o caminho vem do ambiente.
- **Sem `mkdir` defensivo da base**: criar `/tmp/opencode` (como fazem
  `test_poweroff.cpp`, `test_known_hosts_file.cpp` e `test_cat_worker_runtime.cpp`)
  resolveria o sintoma mantendo o acoplamento ao diretório local; escolhi a base
  genérica, que é o objetivo do despacho.
- **Documentação não alterada**: o despacho disse "docs não". Confirmei que
  `code-map.md` descreve este alvo (linhas ~2138-2150) **sem** citar a base
  temporária, então nada no mapa fica stale com a correção. Considerei uma
  frase no mapa por causa da regra do `AGENTS.md` e revertí: o mapa documenta
  cenário/símbolos, não o caminho de scratch do fixture, e o diff ficou
  estritamente no arquivo acordado. Se o developer preferir registrar a
  convenção `TMPDIR` no mapa, é uma linha na mesma entrada.
- **`<cstdio>` mantido**: `std::snprintf` deixou de ser usado, mas o include não
  gera warning com `-Werror` e removê-lo ampliaria o diff sem ganho.
- `jev`: não usado — o despacho fixou a alternativa (TMPDIR/`/tmp`), não havia
  empate semântico a desempatar.

## Pending and risks

- **Validação independente pendente**: o tester precisa reexecutar o alvo focado
  (idealmente também com `TMPDIR` custom e com as flags de CI) e é quem pode
  fechar o gate; a CI remota só prova depois de um push, que não fiz.
  `manual_validation` não se aplica (infra de teste host, sem firmware).
- **Risco residual de outros hardcodes da mesma classe** (fora do escopo, não
  corrigidos): `tests/host/keymap/Makefile` tem o alvo `test_music_trace` que
  compila e executa `/tmp/opencode/test_music_trace`; ele **não** está no
  agregado `test` (só `.PHONY` + receita), então não é bloqueio de CI hoje, mas
  quebra em runner limpo se for executado. `test_shell_help.cpp` e
  `test_shell_session.cpp` constroem `cyberdeck_local_shell` em
  `/tmp/opencode/...` (o segundo cria a árvore com `create_directories` na linha
  554); `test_poweroff.cpp`, `test_known_hosts_file.cpp` e
  `test_cat_worker_runtime.cpp` fazem `mkdir("/tmp/opencode", 0700)` com
  `EEXIST` tolerado.
- **Não verifiquei se o agregado passa no runner limpo**: como o CI para no
  primeiro cenário que falha, os alvos do agregado **posteriores** a
  `test_music_metadata_io` (ordem em `Makefile:3101-3125`) ainda não têm
  evidência de execução no GitHub; se algum dos hardcodes acima for exercitado
  por um alvo posterior, o CI pode falhar em outro ponto. Não investiguei nem
  alterei esses arquivos por restrição de escopo.
- **Achado menor (não bloqueante)**: no caminho de falha da base temporária a
  linha de resumo imprime `-1/0 checks passed` (`checks - failures` com
  `checks == 0` e `failures == 1`). É comportamento pré-existente, só alcançável
  quando `mkdtemp` falha, e não foi alterado.
- Nada commitado, nenhuma tag criada, nenhum flash.

## References

- Dispatch do developer (CI run `42441766510`, commit `467f283`).
- Arquivo alterado: `tests/host/keymap/test_music_metadata_io.cpp`
  (`temp_dir_base()`, `make_temp_dir()`).
- Wiring do alvo: `tests/host/keymap/Makefile` (`MUSIC_METADATA_IO_BIN` em
  `BINS`, `.PHONY: test_music_metadata_io`, uma entrada no agregado `test`).
- Step de CI: `.github/workflows/quality-gate.yml:121-148`
  (`Host aggregate and coverage`, `make clean` + `make CXXFLAGS='… -O0 --coverage' test`).
- Convenção de scratch em Python: `tests/host/keymap/test_wifi_audit_contract.py`
  (`_scratch_root`), `tests/host/keymap/test_screen_protection_contract.py`.
- Contexto do alvo: `docs/agent-state/music-track-metadata/coder.md`
  (ciclos 6/9: criação do alvo e as 205 asserções) — nota de outra tarefa, não
  editada.
