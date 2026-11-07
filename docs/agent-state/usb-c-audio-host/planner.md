# planner - usb-c-audio-host

- Role: planner
- Updated: 2026-11-07
- Code state: leitura do worktree atual, HEAD/diff não verificados (sem execução de comandos)

## Dispatch objective

Avaliar a viabilidade de conectar diretamente um fone UAC USB-C à USB-C do Tab5 stock; recomendar implementação apenas se hardware permitir, sem alterar firmware ou configuração nesta etapa.

## Work done

Li integralmente os três arquivos canônicos indicados e consultei `project-map.md`, `code-map.md:2842-2853` e fontes direcionadas. Recomendação: **não implementar host na USB-C stock** com a evidência disponível; manter USB-A host com adaptador USB-A macho → USB-C fêmea adequado a dados e alimentação. O conector USB-C consta como sink por Rd 5,1 kΩ em CC1/CC2 e VBUS `USB_5VIN` na folha 4 do esquemático fornecida no despacho; controlador OTG não basta para fornecer Rp/5 V ao fone. Plano é de decisão/documentação, não de mudança de firmware; hardware modificado ou acessório ativo requerem nova aprovação e análise própria.

## Evidence

- `components/m5stack_tab5/include/bsp/m5stack_tab5.h:103-109`: host existente mapeado a GPIO20/19, habilitação via expander B pino 3; `src/bsp_feature_en.cpp:61-70` aciona essa alimentação; `src/bsp_usb.c:65-95` instala USB Host.
- `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp:1-13,669` usa UAC/USB Host e chama `bsp_usb_host_start`; header `:50-54` especifica USB-A. `components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp:32-33,2397-2412` usa USB Serial-JTAG. `main/idf_component.yml:9-15` fixa driver UAC local; `tests/host/keymap/test_usb_audio_host_contract.py:63-77` protege configuração USB-A.
- Folha 4 do esquema: J8 USB_C_16P, CC1/CC2 → R52/R53 5,1 kΩ para GND, VBUS `USB_5VIN`, D± `USBIN_DP/DM` — evidência encaminhada pelo developer, não PDF reaberto nesta execução; conferir contra versão oficial/revisão da placa antes de afirmar impossibilidade para qualquer revisão.
- Documentação de BSP/ESP-IDF e links externos citados no despacho são evidência de capacidade lógica OTG, **não** de circuito source Type-C. Não executei build, teste, consulta online nem ensaio elétrico; testes existentes não provam USB-C host.

## Decisions and assumptions

Perfil **high**: comutação de role/CC/VBUS e risco de dano por alimentação indevida. Sem Jev: evidência de Rd/VBUS sink não configura empate reversível. Propor REQ-USBC-01 (viabilidade stock), REQ-USBC-02 (não regressão USB-A/console) e REQ-USBC-03 (restrição de segurança); não foram recebidos REQ/AC formalizados do analyst. Nenhum número visível ao usuário é introduzido. A conclusão é para *conexão direta, hardware stock e revisão correspondente ao esquema*; não afirma que os dados USB-C não possam ser OTG em circuito diferente.

## Pending and risks

Confirmar proveniência/revisão da folha 4 no documento oficial M5Stack e, se a decisão depender de exceção de hardware, verificar existência real de controlador Rp/VBUS source e roteamento D± no esquema completo; não ligar VBUS externamente nem testar host por firmware sem plano elétrico aprovado. Aprovação humana do objetivo+plano antes de qualquer edição; somente se houver uma alternativa stock documentada replanejar, com cenários de tester high-risk antes de implementar. Se a USB-A com adaptador for aceita como solução, nenhum código precisa mudar; aceite do usuário de funcionalidade no dispositivo é separado de testes host.

## References

`code-map.md:2842-2853`; `components/m5stack_tab5/{include/bsp/m5stack_tab5.h,src/bsp_usb.c,src/bsp_feature_en.cpp}`; `components/cyberdeck/{include/platform/usb/cyberdeck_usb_audio_host.h,src/platform/usb/cyberdeck_usb_audio_host.cpp,src/apps/serial/cyberdeck_serial_bridge.cpp}`; `tests/host/keymap/test_usb_audio_host_contract.py`; esquema espelhado: https://raw.githubusercontent.com/zhaoker/M5stack_Tab5_AIO_box/main/doc/Tab5_Schematics_PDF.pdf ; página oficial de esquemas: https://docs.m5stack.com/en/core/Tab5?id=schematics ; BSP: https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_tab5/README.md .
