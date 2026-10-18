# Plano 10 — Perfis de missão e escopo

## Objetivo

Definir perfis locais como “casa”, “laboratório” ou “cliente” para explicitar
destinos, limites de execução e retenção dos recursos de diagnóstico.

## Princípios

- Perfil é proteção de UX e política local, não autorização legal nem barreira
  de segurança contra firmware modificado.
- Sem descoberta ou sondagem em lote por padrão; revisão/confirmação explícita
  antes de ativar.
- Separar configuração de rede, credenciais e escopo; perfil não duplica senhas.

## Escopo

- Nome, descrição, destinos permitidos explícitos, orçamento de testes e política
  de retenção de relatórios.
- Seleção visível do perfil ativo; validar destino antes de netdiag; registrar
  mudança sem dados secretos.
- Importação/exportação versionada, confirmada e validada fail-closed.

## Etapas e aceite

Definir schema bounded; implementar parser/modelo puro e persistência apropriada;
ligar a validação ao serviço de diagnóstico (não somente à UI); testar perfil
ausente/corrompido, destino fora do escopo, limites, rollback e mudança durante
operação.

Aceite: destino fora do perfil é rejeitado antes de abrir socket; perfil e limites
aparecem antes do teste; não há bypass silencioso; perfil não contém segredos;
mudança de escopo cancela ou exige confirmação para operação ativa.
