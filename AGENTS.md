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
