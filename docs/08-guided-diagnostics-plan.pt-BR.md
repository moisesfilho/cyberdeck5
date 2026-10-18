# Plano 08 — Diagnóstico guiado por camadas

## Objetivo

Compor resultados existentes num roteiro determinístico para localizar falhas
entre associação Wi-Fi, IP, gateway, DNS, conexão TCP e TLS/HTTP.

## Princípio

Esta camada organiza testes conhecidos; não é agente autônomo e não deixa IA
decidir alvos ou executar ações. Cada passo é visível e cancelável.

## Escopo

- Perfis: “internet indisponível”, “servidor não responde” e “TLS falha”.
- Testar somente destino especificado pelo usuário ou endpoint de referência
  configurado explicitamente.
- Mostrar sequência/resultado com evidência, duração e limites de confiança;
  permitir repetir uma etapa.
- Resumo opcional para IA somente após revisão.

## Etapas e aceite

Definir máquina de estados pura; ligar etapas à app netdiag; impor orçamento
global de tempo/tentativas; testar sucesso/falha, cancelamento e perda de Wi-Fi;
integrar a UI.

Aceite: sem passos ocultos ou varredura; falha não gera retry ilimitado; saída
distingue observação de hipótese; resultados completos estão disponíveis sem IA.
