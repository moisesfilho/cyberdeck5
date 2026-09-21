# Host tests: getnameinfo dual-stack (components/sock_utils)

Testes unitarios host-side do `getnameinfo` dual-stack do componente local
`components/sock_utils` (overlay do `espressif/sock_utils` v0.2.2 que o
`david-cermak/libssh` usa via dependency transitiva).

Padrao seguido: `managed_components/espressif__sock_utils/test/host/` — projeto
ESP-IDF de host tests com **target linux** (`CONFIG_IDF_TARGET="linux"`),
Catch2 v3 e `esp_getnameinfo` (namespace `esp_` local do header do componente,
que evita colisao com a glibc exatamente como o componente oficial faz).

## O bug que este teste fixa

O libssh (`ssh_connect_host_nonblocking`, `libssh-mirror/src/connect.c`:

- resolve o host com `getaddrinfo(AF_UNSPEC, AI_NUMERICHOST|AI_NUMERICSERV)`;
- para **cada** `struct addrinfo` (IPv4 **ou** IPv6), chama
  `getnameinfo(ai_addr, ai_addrlen, addrname, NI_MAXHOST, portname, NI_MAXSERV,
  NI_NUMERICHOST | NI_NUMERICSERV)` (connect.c:286-292);
- se `getnameinfo` falhar, o addrinfo e **descartado** (connect.c:293-300) —
  e se o unico resultado for IPv6 ou IPv4-mapped, a conexao falha.

O `espressif__sock_utils` oficial implementa `getnameinfo` so para `AF_INET`
(retorna `EAI_FAMILY` para `AF_INET6`, src/getnameinfo.c:49-50): qualquer alvo
IPv6/IPv4-mapped era descartado pelo libssh. Este componente local corrige o
dual-stack **sem alterar `managed_components`**. A implementacao local tambem
mantem os demais utilitarios do componente (`ifaddrs`, `gai_strerror`,
`socketpair` e `gethostname`).

## Estrutura

```
components/sock_utils/
└── test/
    └── host/                  <- este projeto de host tests (target linux)
        ├── CMakeLists.txt
        ├── sdkconfig.defaults
        ├── README.md
        └── main/
            ├── CMakeLists.txt
            ├── idf_component.yml
            └── test_getnameinfo.cpp
```

## Como rodar

```sh
cd components/sock_utils/test/host
idf.py set-target linux
idf.py reconfigure
idf.py build
./build/sock_utils_host_test.elf
```

O `idf_component.yml` do teste declara `sock_utils` com
`override_path: "../../../"`: o component manager substitui a dependencia
registry `espressif/sock_utils` (pedida pelo libssh) pela copia local
`components/sock_utils` (mesmo mecanismo do teste oficial do sock_utils).

### Workaround de libbsd (somente se necessario)

Em hosts nos quais os headers de desenvolvimento do libbsd nao estao
disponiveis, o build pode falhar ao incluir `bsd/string.h`. Nesse caso, use um
staging local do pacote libbsd e exporte os caminhos antes de configurar o
projeto:

```sh
export CPATH=/tmp/opencode/bsd/root/usr/include/x86_64-linux-gnu
export CPLUS_INCLUDE_PATH=/tmp/opencode/bsd/root/usr/include/x86_64-linux-gnu
export LIBRARY_PATH=/tmp/opencode/bsd/lib:/tmp/opencode/bsd/root/usr/lib/x86_64-linux-gnu
idf.py -DCMAKE_LIBRARY_PATH=/tmp/opencode/bsd/root/usr/lib/x86_64-linux-gnu reconfigure
idf.py -DCMAKE_LIBRARY_PATH=/tmp/opencode/bsd/root/usr/lib/x86_64-linux-gnu build
./build/sock_utils_host_test.elf
```

Os caminhos acima sao especificos do staging usado na validacao local; nao
devem ser exportados quando o sistema ja fornece libbsd normalmente.

O harness g++ do projeto **nao** substitui este host test: o
`tests/host/keymap/Makefile` (g++ puro + shims) **nao** comporta este teste:
ele depende do lwIP do ESP-IDF (`inet_ntop` AF_INET6, `lwip/netdb.h`),
do ambiente de build ESP-IDF (headers POSIX `lwip/port`) e do Catch2 v3 —
dependencias que nao existem no Makefile host-side atual. Por isso estes
testes ficam no harness oficial de host tests do ESP-IDF (**target linux**),
dentro do componente (padrao oficial). Nenhuma alteracao foi feita em
`managed_components`, `tests/host/keymap` ou codigo de producao.

## Override e validacao

O firmware declara o mesmo override em `main/idf_component.yml`, com
`override_path: "../components/sock_utils"`, substituindo a dependencia
transitiva do libssh. O host test usa `override_path: "../../../"` porque seu
arquivo `idf_component.yml` esta em `components/sock_utils/test/host/main/`.
Assim, tanto o firmware quanto o teste usam a mesma implementacao dual-stack
local, sem modificar `managed_components`.

Validacao registrada para a correcao:

```sh
idf.py reconfigure
idf.py build
```

No firmware, o build deve compilar `components/sock_utils/src/getnameinfo.c`
local e nao referenciar `managed_components/espressif__sock_utils`. No host
test, o executavel produzido e `build/sock_utils_host_test.elf`.

Limitacoes conhecidas:

- O host test depende do ESP-IDF, do target linux, do Catch2 v3 e do lwIP; nao
  e coberto pelo harness `tests/host/keymap`.
- O formato de IPv4-mapped e `::FFFF:a.b.c.d`, em maiusculas, conforme o
  `inet_ntop` do lwIP, e nao o formato usual da glibc.
- Buffer insuficiente retorna `EOVERFLOW` sem truncagem, uma divergencia
  intencional do componente oficial.
- `flags == 0` nao faz DNS reverso; essa limitacao e herdada do componente
  oficial e os testes exigem que os buffers permanecam intactos.

## Contrato fixado pelas assercoes

| Entrada | Flags | Saida esperada (rc=0) |
| --- | --- | --- |
| AF_INET 192.168.1.50 / 10.0.0.1 / 0.0.0.0 / 255.255.255.255 | NI_NUMERICHOST | `192.168.1.50` etc. |
| AF_INET + porta 2222 | NI_NUMERICHOST\|NI_NUMERICSERV | host + `2222` (flags do libssh) |
| AF_INET porta 443 | NI_NUMERICSERV\|NI_DGRAM | `443` |
| AF_INET6 2001:db8::1 / ::1 / :: / fe80::1 | NI_NUMERICHOST | `2001:db8::1`, `::1`, `::`, `fe80::1` |
| AF_INET6 IPv4-mapped ::ffff:a.b.c.d | NI_NUMERICHOST | `::FFFF:a.b.c.d` (formato do lwIP, ip6_addr.c IP4MAPPED_HEADER) |
| flags genuinamente fora de {NI_NUMERICHOST=0x04,NI_NUMERICSERV=0x08,NI_DGRAM=0x10} | (0x01, 0x02, 0x20, 0x2C, 0x1000, -1) | `3` (EAI_BADFLAGS), buffers intactos |
| hostlen/servlen insuficientes | NI_NUMERICHOST/NI_NUMERICSERV | `75` (EOVERFLOW), sem truncagem |
| AF_UNIX / AF_UNSPEC / familia desconhecida | NI_NUMERICHOST | `204` (EAI_FAMILY lwip) |
| addr NULL / addrlen insuficiente | NI_NUMERICHOST | != 0, sem crash |
| contrato libssh: getaddrinfo(AF_UNSPEC, AI_NUMERICHOST\|AI_NUMERICSERV) | NI_NUMERICHOST\|NI_NUMERICSERV por ai | rc=0 com host/serv numericos corretos |

## Decisoes de contrato (para auditoria do reviewer)

1. **IPv4-mapped -> `::FFFF:a.b.c.d`** (maiusculo, formato nativo do lwIP
   `ip6addr_ntoa_r`). Alternativa considerada: `::ffff:a.b.c.d` (glibc) e
   `a.b.c.d` puro (estilo conectavel). Escolhido o formato lwIP: e o que
   `inet_ntop` do lwIP produz (implementacao mais provavel e POSIX), e o
   libssh usa o resultado apenas para LOG do endereco (connect.c:303-307) —
   o `connect()` usa o `sockaddr` original, nao esta string.
2. **Buffer de servico insuficiente -> EOVERFLOW (75)**: divergencia
   intencional do oficial, que truncava via `snprintf` e retornava 0
   (bug de truncagem silenciosa, src/getnameinfo.c:42-44).
3. **addr NULL / addrlen curto -> erro seguro**: o oficial tinha
   comportamento indefinido nessas entradas; o teste exige erro, sem fixar
   o codigo exato (EAI_FAIL/EAI_FAMILY fica a criterio do coder).
4. **Codigos de retorno**: EAI_BADFLAGS=3 e EAI_FAMILY=204 sao os valores do
   ecossistema (netdb_macros.h / lwip/netdb.h), consistentes em host linux e
   device. EOVERFLOW vem de `<cerrno>` (75 nos dois ambientes).
5. **NI_NUMERICSERV divergente glibc (0x02) vs ESP-IDF (0x08)**: o teste usa
   sempre as macros do componente (`netdb_macros.h`), nunca as da glibc, para
   ser fiel ao que o componente valida em cada ambiente.
6. **flags == 0 -> sucesso sem DNS reverso**: limitacao herdada do oficial
   (nao ha reverse DNS no lwIP); o teste fixa a ausencia de efeitos colaterais
   (buffers intactos).

## Arquivos de referencia (nao alterados)

- Padrao oficial: `managed_components/espressif__sock_utils/test/host/`
- Implementacao oficial (IPv4-only, base do diff): `.../src/getnameinfo.c`
- Contrato consumidor: `managed_components/david-cermak__libssh/libssh-mirror/src/connect.c`
