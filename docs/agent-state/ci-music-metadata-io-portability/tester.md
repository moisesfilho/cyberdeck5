# tester - ci-music-metadata-io-portability

- Role: tester
- Updated: 2026-11-08 00:24 (ciclo 1: validação independente da portabilidade do fixture)
- Code state: `develop` HEAD `467f283dce70c79b26e1d0d0bb8203e3136afe66` + worktree
  sujo com **um** arquivo rastreado modificado (`tests/host/keymap/test_music_metadata_io.cpp`).
  Snapshot verificado por mim, independente da nota do coder:
  `git diff | sha256sum` = `7dc4df38af6ac2829aeb12d1eb1144946491f9ef72255c037f9e03fcd86e20e1`
  (idêntico ao reportado pelo coder), `git diff --name-only` = 1 arquivo,
  `git diff --stat` = 8 inserções / 2 remoções, sha256 do arquivo (16 hex) =
  `495b5e097f296ac7`, `git diff --check` exit 0.
  `git status --porcelain` atual = ` M tests/host/keymap/test_music_metadata_io.cpp`
  + `?? docs/agent-state/ci-music-metadata-io-portability/` (a nota do coder e esta).

## Dispatch objective

Validar de forma independente, sem editar produção/testes e sem suíte global, build
IDF, flash ou commit, a correção do coder para a falha de CI do run `42441766510`
(commit `467f283`): `test_music_metadata_io` usava base fixa `/tmp/opencode` no
`mkdtemp` e quebrava em runner sem esse diretório. Critério de saída: executar o
alvo focado com ambiente default e com `TMPDIR` custom existente **sem** filho
`opencode`, confirmar contagem de checks e limpeza, e confirmar escopo/hash do diff.

## Work done

1. Li `AGENTS.md` (raiz e `cyberdeck5`), `code-map.md` (entrada do alvo em
   ~2138-2155), o wiring do alvo no `tests/host/keymap/Makefile`
   (`MUSIC_METADATA_IO_BIN` em `BINS`, `.PHONY: test_music_metadata_io`,
   entrada no agregado `test` em `Makefile:3112`) e o step de CI em
   `.github/workflows/quality-gate.yml:121-148` (`make clean` + `make
   CXXFLAGS='-std=c++17 -Wall -Wextra -Werror -O0 --coverage' test`).
2. Revisei o diff (único hunk em helpers, linhas ~373-388): novo
   `temp_dir_base()` (`TMPDIR` não vazio, senão `"/tmp"`) e `make_temp_dir()`
   compondo `std::string`. `mkdtemp` continua sendo a única fonte de unicidade,
   `#include <cstdlib>` já existia, `main()` preserva o `std::cerr` de falha e o
   `rm -rf '<dir>'` sobre o caminho devolvido por `mkdtemp`.
3. Confirmei que o artefato compilado não contém mais a base fixa
   (`strings` do binário) e que o arquivo-fonte não tem ocorrência de
   `opencode`.
4. Executei o alvo focado em 4 configurações de ambiente + 2 controles
   negativos + 3 repetições por configuração (abaixo).
5. Fiz um probe standalone **fora do repositório**
   (`/tmp/opencode/mkdtemp_probe2.cpp`) para provar a sensibilidade da borda
   `TMPDIR=""` sem editar o teste: mutação equivalente em alvo separado.
6. Recompilei com as flags exatas do CI (removendo o `_bin` para forçar
   recompilação, já que troca de `CXXFLAGS` não é pré-requisito) e restaurei o
   artefato default depois.

Nada foi editado além desta nota: `git diff --name-only` permaneceu com 1
arquivo durante todo o trabalho; nenhum artefato de cobertura ficou no worktree
(o `.gcda`/`.gcno`/`_bin` instrumentado foram removidos e o `_bin` foi
recompilado com as flags default `-O2`).

## Cenários e rastreabilidade

Não há `REQ-*`/`AC-*` formais nesta tarefa; os critérios do despacho foram
traduzidos em cenários `TEST-PORT-*`:

| ID | Cenário | Origem |
| --- | --- | --- |
| TEST-PORT-01 | Alvo focado com ambiente default (`TMPDIR` unset) roda 205/205 | critério "ambiente default" |
| TEST-PORT-02 | `TMPDIR` custom existente, sem filho `opencode`, roda 205/205 e cria o diretório **na base** | critério "TMPDIR custom sem opencode" |
| TEST-PORT-03 | Limpeza: base volta vazia após a execução e `rmdir` da base funciona | critério "confirm cleanup" |
| TEST-PORT-04 | Contagem de checks preservada (205/205) e superfície de assert igual ao HEAD | critério "checks count" |
| TEST-PORT-05 | Base derivada do ambiente: `TMPDIR` inexistente → `FAIL: cannot create the temp directory`, exit 1 | controle negativo / sensibilidade |
| TEST-PORT-06 | `TMPDIR` existente sem permissão de escrita → falha, **sem** fallback silencioso | controle negativo |
| TEST-PORT-07 | Borda `TMPDIR=""` → fallback `/tmp`, 205/205 | borda da condição `tmpdir[0] != '\0'` |
| TEST-PORT-08 | `env -u TMPDIR` → fallback `/tmp`, 205/205 | caminho sem a variável |
| TEST-PORT-09 | Base com espaço no nome (`/tmp/td space.XXXXXX`) → 205/205 e limpeza (quoting do `rm -rf`) | portabilidade do caminho |
| TEST-PORT-10 | Compila e passa com as flags exatas do CI (`-Werror -O0 --coverage`) | step de CI que falhou |
| TEST-PORT-11 | Determinismo: 3 execuções idênticas por configuração, sem resíduo | isolamento/repetibilidade |

## Evidence

Raiz `/home/moises/Projetos/cyberdeck5`, sem `clean`, sem agregado, sem build IDF,
sem flash, sem commit. `TMPDIR` do meu shell estava unset
(`TMPDIR=[<unset>]`), logo o default local exercita o fallback `/tmp`;
`/tmp/opencode` existe nesta máquina, o que dá força aos controles negativos.

| Comando | Resultado |
| --- | --- |
| `make -C tests/host/keymap test_music_metadata_io` (default) | `205/205 checks passed`, exit 0; nenhuma recompilação (binário do coder já atual: bin 00:19:31 > fonte 00:17:11) |
| `strings -a test_music_metadata_io_bin \| grep opencode` | nenhuma ocorrência; único literal é `/music-metadata-io-XXXXXX` ⇒ hardcode ausente do artefato |
| `BASE=$(mktemp -d /tmp/td-portability.XXXXXX)` (sem filho `opencode`) + `TMPDIR="$BASE" ./test_music_metadata_io_bin` | `205/205 checks passed`, exit 0 |
| mesma execução: `stat -c %y "$BASE"` e `ls -A "$BASE"` antes/depois | mtime `00:22:35.619756071` → `00:22:35.634756101` e `[]` → `[]` ⇒ `mkdtemp` criou subdiretório **dentro da base** e a limpeza o removeu |
| `rmdir "$BASE"` após a execução | OK; `ls -d /tmp/music-metadata-io-*` = 0 resíduos |
| `TMPDIR=/tmp/definitely-missing-base-945020 ./test_music_metadata_io_bin` | `FAIL: cannot create the temp directory` + `-1/0 checks passed`, exit 1 ⇒ caminho vem do `TMPDIR` (com o hardcode anterior este caso passaria, pois `/tmp/opencode` existe aqui) |
| base `chmod 500` + `TMPDIR=<base> ./...` | `FAIL: cannot create the temp directory`, exit 1, sem fallback silencioso para `/tmp`; base restaurada (`chmod 700`) e removida |
| `TMPDIR= ./test_music_metadata_io_bin` | `205/205 checks passed`, exit 0 ⇒ guarda de string vazia funciona |
| `env -u TMPDIR ./test_music_metadata_io_bin` | `205/205 checks passed`, exit 0 |
| `BASE=$(mktemp -d "/tmp/td space.XXXXXX")` + `TMPDIR="$BASE" ./...` | `205/205 checks passed`; `ls -A` vazio depois e `rmdir` OK |
| `rm -f test_music_metadata_io_bin` + `make -C tests/host/keymap CXXFLAGS='-std=c++17 -Wall -Wextra -Werror -O0 --coverage' test_music_metadata_io` | recompilou de fato (linha `g++ … -Werror -O0 --coverage …` visível), `205/205 checks passed`, exit 0, sem warnings |
| restauração: `rm -f test_music_metadata_io_bin test_music_metadata_io_bin-*` + `make … test_music_metadata_io` | recompilou com `-O2` default, `205/205 checks passed`; worktree só com o `.cpp` + `.gitignore`-covered `_bin` |
| 3 execuções default e 3 com `TMPDIR` custom | 6/6 exit 0; 0 resíduos em `/tmp/music-metadata-io-*` |
| `grep -c "CHECK(" <arquivo>` vs `git show HEAD:… \| grep -c "CHECK("` | `124` = `124` ⇒ superfície de assert idêntica ao HEAD (205 execuções por causa de loops) |
| probe `/tmp/opencode/mkdtemp_probe2.cpp` (mutação equivalente fora do repo) | `"/music-metadata-io-XXXXXX"` (base vazia) → `NULL errno=13 Permission denied` ⇒ sem a guarda de string vazia, TEST-PORT-07 falharia; base inexistente → `NULL errno=2` (correlaciona TEST-PORT-05); base existente → cria e limpa dentro dela |

Não executados (fora do escopo explícito): `clean`, agregado
`make -C tests/host/keymap test`, `verify`, cobertura/gcovr, ASan/UBSan/TSan,
alvos irmãos (`test_music_id3_contract` etc.), `idf.py build`, flash, commit.
Nada nesta nota significa CI remoto verde.

## Decisions and assumptions

- **Hash de status**: o coder reportou
  `git status --porcelain | sha256sum` = `9737a135…f213b`; hoje esse valor não se
  reproduz porque passou a existir o diretório untracked
  `docs/agent-state/ci-music-metadata-io-portability/`. Reproduzi o hash do coder
  digitando a única linha `" M tests/host/keymap/test_music_metadata_io.cpp"`
  ⇒ `9737a135…f213b` **exato**. Logo a divergência atual é só artefato de nota,
  não alteração de código; o hash que importa (`git diff`) está idêntico.
- **Não editei testes**: apesar de a entrega ser de fixture, o despacho proíbe
  alterar testes. A sensibilidade da borda `TMPDIR=""` foi provada com mutação
  equivalente em programa standalone em `/tmp/opencode` (não versionado), em vez
  de mutar o arquivo do repositório.
- **Base vem do ambiente, sem `mkdir` defensivo**: concordo com a escolha do
  coder; criar `/tmp/opencode` (padrão de `test_poweroff.cpp`,
  `test_known_hosts_file.cpp`, `test_cat_worker_runtime.cpp`) manteria o
  acoplamento local. Os controles TEST-PORT-05/06 provam que a base é derivada
  do `TMPDIR` e que misconfiguração falha em vez de mascarar.
- **`code-map.md` não ficou stale**: a entrada do alvo descreve cenário/símbolos
  e não cita base temporária; os `/tmp/opencode` presentes no mapa são logs
  históricos de evidência (linhas ~800/1359/1385/2063), não o fixture.
- **Ordem do agregado após este alvo** (verificado por leitura, não por execução):
  restam dependentes de `/tmp/opencode` no agregado —
  `test_poweroff_bin` (`Makefile:3130`), `test_cat_worker_runtime` (3132),
  `test_known_hosts_file_bin` (3133) e depois `test_shell_session_bin` (3210),
  `test_shell_help_bin` (3212). Os três primeiros criam a base com
  `mkdir(...) == 0 || errno == EEXIST`, e executam **antes** dos shells, então em
  runner limpo a base existe quando `test_shell_session`/`test_shell_help` rodam.
  `test_music_trace` (hardcode no Makefile) não está no agregado (0 ocorrências na
  receita `test`).
- **`jev`**: não usado — não havia escolha semântica em aberto; a alternativa e a
  borda estavam fixadas pelo despacho e pelo código.

## Pending and risks

- **CI remoto continua não provado**: só um push reexecuta o step
  `Host aggregate and coverage` (run `42441766510`). Localmente provei apenas o
  alvo focado, por restrição de escopo.
- **Sem guarda estrutural contra reintrodução do hardcode** (lacuna de cobertura
  relevante, não bloqueante): nada em `test_music_id3_contract.py` ou em contrato
  equivalente pinaria "a base do fixture deriva de `TMPDIR`". Um contrato de uma
  linha (proibir `/tmp/opencode` em `test_music_metadata_io.cpp`) evitaria
  regressão silenciosa. Não implementei por proibição de editar testes.
- **Acoplamento de ordem remanescente** (pré-existente, fora do escopo):
  `test_shell_help.cpp:162` e `test_shell_session.cpp:163,554` assumem
  `/tmp/opencode` existente e só funcionam no agregado porque `test_poweroff_bin`
  executa antes. Rodar `./test_shell_session_bin` isolado em runner limpo
  falharia. O mesmo vale para `test_music_trace` no Makefile (não agregado).
- **`TMPDIR` com aspas simples** quebraria o `rm -rf '<dir>'` (pré-existente, não
  alterado); caminho com espaço foi validado e limpa corretamente.
- **Cosmético pré-existente**: no caminho de falha a linha final imprime
  `-1/0 checks passed` (`checks - failures` com `checks == 0`). Não alterado;
  não induz exit code errado (segue 1).
- Nenhuma validação manual de hardware se aplica (infra de teste host, sem
  firmware). Nada commitado, nenhuma tag, nenhum flash, nenhum artefato de
  cobertura deixado no worktree.

## References

- Despacho do developer: CI run `42441766510`, commit `467f283`.
- Nota do coder: `docs/agent-state/ci-music-metadata-io-portability/coder.md`.
- Arquivo alterado: `tests/host/keymap/test_music_metadata_io.cpp`
  (`temp_dir_base()`, `make_temp_dir()`, linhas ~373-388).
- Wiring: `tests/host/keymap/Makefile:243,249-250,2937-2942,3112`.
- Step de CI: `.github/workflows/quality-gate.yml:121-148`.
- Índice: `code-map.md` (~2138-2155); convenção Python de scratch em
  `/tmp`: `tests/host/keymap/test_wifi_audit_contract.py` (`_scratch_root`).
