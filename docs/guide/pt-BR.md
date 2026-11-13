# MD Rewriter: guia passo a passo, da caixa ao primeiro jogo

**Idiomas:** [English](en.md) · [Português (Brasil)](pt-BR.md) · [Русский](ru.md) · [简体中文](zh-Hans.md)

Para quem acabou de receber a placa MD Rewriter e sabe pouco de eletrônica. Tudo aqui foi feito numa unidade real em 13/11/2026; os resultados estão na seção 9. Se algo não bater com o que você vê, pare e leia a seção 10 antes de continuar.

**Resumo.** A placa funciona de fábrica com o firmware 1.0 (sem solda). O firmware 1.1 é opcional: grava cartuchos flash corretamente de uma vez e reconhece mais chips, mas instalá-lo exige soldar um pequeno conector na placa, ter um gravador ST-Link V2 e cerca de uma hora. **Os cartuchos flash da família KY / FlashKit guardam no máximo 2 MiB de jogo (seção 8).**

---

## 0. Regras de segurança

1. **Nunca grave, apague nem "teste" um cartucho original de jogo** (os vendidos com jogos). São ROMs de máscara: não podem ser gravadas, e a tentativa só pode causar dano. O programa lê esses cartuchos com segurança; é só isso que você faz com eles.
2. **Uma única fonte de energia por vez.** Ou o cabo USB da placa, ou o fio de 3,3 V do ST-Link, nunca os dois.
3. **Cartuchos só são inseridos e retirados com o cabo USB desconectado.**
4. **Faça backup antes de apagar ou sobrescrever qualquer coisa** (um cartucho flash que tenha um jogo, e o firmware da placa).
5. O ferro de solda queima (cerca de 350 °C). Trabalhe em local ventilado, lave as mãos depois de soldar (a solda pode conter chumbo) e deixe o ferro esfriar antes de guardá-lo.
6. Toque num objeto de metal aterrado antes de pegar a placa e não a segure pelo slot do cartucho.

---

## 1. O que você precisa

| Item | Para quê | Necessário? |
|------|----------|-------------|
| A placa MD Rewriter (V1.0) | o dispositivo | sim |
| Cabo USB **com fios de dados**, de preferência USB-A para USB-C (veja 3.2) | liga a placa ao computador | sim |
| Computador com Linux (testado) ou Windows | executa o programa | sim |
| Cartucho flash (S29GL064N, tipo KY / FlashKit) | gravar jogos | para gravar |
| Cartucho de jogo de Mega Drive | fazer backup | para ler |
| Gravador ST-Link V2 (original ou o clone comum em forma de pendrive USB) | instalar o firmware 1.1 | opcional |
| Barra de pinos 1×4, 2,54 mm | soldar nos pads SWD | com o ST-Link |
| 4 fios jumper Dupont fêmea | ligar o ST-Link à barra de pinos | com o ST-Link |
| Ferro de solda de 25 a 40 W com ponta fina, solda 0,6 a 0,8 mm com fluxo | soldar a barra de pinos | com o ST-Link |
| Malha dessoldadora, caneta de fluxo, álcool isopropílico, escovinha | limpar e refazer soldas | ajuda muito |
| Multímetro com teste de continuidade (buzzer) | conferir soldas e pads | com o ST-Link |
| Fita adesiva ou garra "terceira mão" | segurar a placa | ajuda |

---

## 2. Conheça a placa

![Face de cima da placa](../images/board-top.png)

| O que você vê | O que é |
|---------------|---------|
| Duas fileiras de 32 furos no alto, "CART FRONT" e uma seta | o **slot do cartucho**. A seta mostra o lado do cartucho que fica para a frente: a etiqueta do cartucho olha para o mesmo lado da seta |
| Conector USB-C embaixo à direita | liga ao computador e alimenta a placa |
| Botão RESET | reinicia o microcontrolador (não é necessário no uso normal) |
| LEDs POWER e ACT | POWER acende quando há energia; ACT é o LED de atividade |
| Chip U1 (o quadrado grande) | o microcontrolador (STM32F103) |
| Quatro furos redondos em coluna, à direita de U1 e acima do USB-C, sob o capacitor C10 | os **pads SWD**, usados só para instalar firmware. A placa não tem legendas (seção 3.1) |
| Quatro furos nos cantos | furos de fixação M3. Use espaçadores ou pés de borracha para que a parte de baixo da placa não toque a mesa |

![Face de baixo da placa](../images/board-bottom.png)

---

## 3. Soldagem

Você só solda se for instalar o firmware 1.1 (seção 6). Se mantiver o firmware de fábrica, pule para a seção 4.

### 3.1 A barra de pinos SWD (4 pinos)

Os quatro pads, de cima para baixo (conector USB-C na parte de baixo da placa):

```
   ●  3V3
   ●  SWCLK
   ●  SWDIO
   ●  GND
 [USB-C]
```

**Antes de soldar.** Desligue tudo. Confirme os pads com o multímetro (placa sem energia):

1. Modo buzzer: uma ponta no pad de baixo (GND), a outra na carcaça metálica do conector USB-C. Tem que apitar. Se não apitar, o pad está errado ou a placa está de cabeça para baixo.
2. Depois, com o cabo USB ligado, o pad de cima mostra 3,3 V em relação ao GND.

**Soldando, passo a passo:**

1. Prenda a placa na mesa com fita, face de cima para cima. Use a foto da face de cima para achar os quatro furos.
2. Empurre a barra 1×4 nos furos por cima, pinos curtos para baixo, plástico apoiado na placa. Os quatro pinos têm que ficar retos e rentes.
3. Vire a placa. Esquente o ferro a cerca de 330 a 350 °C e limpe a ponta numa esponja úmida.
4. Encoste a ponta no pino e no pad ao mesmo tempo (um segundo), depois leve a solda à junção (não à ponta) por 1 a 2 segundos, tire a solda e depois o ferro. Faça só um pino primeiro.
5. Olhe: a solda tem que ficar brilhante e em forma de pequeno vulcão, cobrindo o pad e o pino. Se estiver fosca, cinza, em bolinha ou deixar fresta, esquente de novo com um pouco de solda nova, ou retire com a malha e refaça.
6. Confira se a barra continua reta e rente. Corrija reaquecendo a primeira solda enquanto empurra. Depois solde os outros três pinos (cerca de 3 segundos cada, nunca mais: calor demais descola os pads).
7. Limpe a placa com álcool isopropílico e escovinha. Inspecione as quatro soldas com lupa ou com a câmera do celular: sem ponte de solda entre vizinhos, sem falhas.
8. **Conferência com o multímetro, placa desligada:**
   - Pad GND: buzzer até a carcaça do USB-C (como antes).
   - Entre cada par de pinos vizinhos (3V3 e SWCLK, SWCLK e SWDIO, SWDIO e GND): **não** pode apitar. Apito significa ponte de solda: retire com a malha.
   - Mexa em cada pino com a unha: nenhum pode balançar. **Um pino de GND solto nos custou horas na primeira unidade** (a conexão com o ST-Link caía a cada poucos minutos e parecia defeito de software). Se algo balançar, reaqueça esse pino.

### 3.2 USB-C: o cabo

Na placa V1.0 o conector USB-C não tem resistores de 5,1 kΩ nas linhas CC (achado H1 de `docs/HARDWARE_REVIEW.md`). Um cabo USB-C para USB-C ligado num carregador ou num notebook recente pode então **não dar energia**. Use um **cabo USB-A para USB-C com fios de dados**; ele sempre funciona. Quem tem prática pode corrigir soldando dois resistores 0603 de 5,1 kΩ dos pads A5 e B5 do conector ao GND; se você não sabe soldar peças 0603, use o cabo A para C.

---

## 4. Montagem e desmontagem

### 4.1 Montagem (tudo desligado)

Para o **uso normal** (sem mexer no firmware): ligue o cabo USB na placa e no computador, depois insira o cartucho (4.3).

Para **instalar o firmware** (seção 6): ligue o ST-Link primeiro, com o cabo USB da placa **desconectado** e o slot do cartucho **vazio**.

1. Só ligue o ST-Link numa porta USB do computador depois que os fios estiverem conectados.
2. Conecte os fios Dupont sempre pelo nome impresso no ST-Link, nunca pela cor ou posição (os modelos clone variam):

| Pino da barra (de cima para baixo) | Rede | Fio do ST-Link |
|------------------------------------|------|----------------|
| 1 | 3V3 | `3.3V` (veja a nota de energia abaixo) |
| 2 | SWCLK | `SWCLK` |
| 3 | SWDIO | `SWDIO` (alguns clones imprimem `SWIO`) |
| 4 | GND | `GND` |
| nenhum | NRST | deixe o `RST` solto: a placa não tem pad de NRST |

O pino `5V` do ST-Link fica sem ligar.

**Nota de energia.** O modo preferido, que foi estável na bancada: fios `GND`, `SWCLK` e `SWDIO` apenas, **sem** o fio `3.3V`, e a placa alimentada pelo próprio cabo USB. Se não puder ligar a placa ao computador, use o fio `3.3V`, com o cabo USB desconectado e o slot vazio; a saída de 3,3 V do clone é fraca, então faça os testes curtos.

3. Revise as quatro ligações. Aperte cada conector Dupont com o dedo: um conector frouxo é o problema mais comum.

### 4.2 Desmontagem depois do trabalho

1. Feche o programa. 2. Desconecte o cabo USB da placa. 3. Espere alguns segundos até os LEDs apagarem. 4. Retire o cartucho (4.3). 5. Puxe os conectores Dupont da barra **pelo plástico, em linha reta**, nunca pelo fio. 6. Desconecte o ST-Link do computador.

A barra de pinos pode ficar na placa; não faz mal. Para tirá-la: esquente um pino e puxe com alicate; ou corte o plástico com alicate de corte e retire os pinos um a um; limpe os furos com a malha. Nunca aqueça um pad por mais que poucos segundos: ele descola. Guarde a placa em saco antiestático ou caixa, longe de metal.

### 4.3 Inserir e retirar um cartucho

- Com o **cabo USB desconectado**.
- Segure o cartucho pelo plástico com a **etiqueta voltada para a seta "CART FRONT"** da placa. Empurre reto para baixo no slot até parar, de forma uniforme, sem balançar nem torcer.
- Para retirar, puxe reto para cima, também com o USB desconectado. Não puxe pela placa nem pelo cabo USB.
- Não troque cartuchos com a placa ligada: o cartucho e a placa dividem as mesmas linhas e podem se danificar.

---

## 5. Instale o programa

Siga "Instalação do aplicativo" no README (Linux: um `.deb` ou um tar.gz da página de releases, ou compile do código; Windows: compile do código). No Linux, dê ao seu usuário acesso à porta serial: `sudo usermod -aG dialout $USER` e faça login de novo, ou instale a regra udev `Software/linux/99-md-rewriter.rules`.

Confira se o programa fala com a placa: ligue a placa (sem cartucho), abra o programa, clique em **Atualizar** e depois em **Conectar**. O registro mostra `Device identified.`. Na linha de comando:

```
mdrewriter-cli ports        # a placa aparece como 0483:5740
mdrewriter-cli info         # mostra a versão do firmware
```

Se aparecer "permission denied", corrija a permissão da porta serial (acima). Se a placa não aparecer, use outro cabo (com fios de dados).

---

## 6. Instale o firmware 1.1 (opcional, cerca de uma hora)

Por quê: com o firmware 1.0, uma gravação no cartucho flash S29GL064N deixa metade das palavras em branco (ele grava uma palavra sim, outra não) e precisa de oito passadas de reparo. O firmware 1.1 grava e verifica um jogo de uma vez e dá nome ao chip. O firmware 1.1 é o mesmo programa com a espera de gravação acrescentada; foi testado na primeira unidade (seção 9).

Você precisa do ST-Link V2, da barra de pinos soldada (seção 3) e dos programas `st-info` e `st-flash` (Linux: `sudo apt install stlink-tools`). O script `tools/swd/swd.sh` precisa de Linux ou macOS com `bash`. No Windows use os mesmos comandos à mão (6.5).

### 6.1 Ligue os fios

Seção 4.1: fios `GND`, `SWCLK`, `SWDIO`, cabo USB da placa ligado, slot do cartucho vazio.

### 6.2 Teste a conexão

```
tools/swd/swd.sh probe
```

Esperado: `swd: probe ok (0x414, 524288 bytes)` (as unidades que temos usam um STM32F103xE com 512 KiB; `0x410` com 131072 bytes também passa). `chip id 0x0` ou "no ST-Link found" indica conexão ruim: confira **primeiro o GND na barra de pinos**, depois SWCLK e SWDIO (troque os dois se a conexão nunca funcionar), depois o cabo. O script repete sozinho uma conexão que cai.

### 6.3 Faça backup do firmware de fábrica

```
tools/swd/swd.sh backup --out ~/md_rewriter_backups/fw-factory
```

Ele lê o chip duas vezes, compara as duas leituras e grava `fw-factory.bin` com um arquivo `.sha256`. **Se disser que a leitura está em branco, pare**: a proteção de leitura está ligada e o firmware de fábrica não pode ser salvo; não apague nem grave. Copie o backup para um segundo lugar (outro disco ou um pendrive).

### 6.4 Grave o firmware 1.1

Baixe `md-rewriter-firmware-1.1.0.bin` (e o `.sha256`) da página de releases deste repositório, confira com `sha256sum -c` e:

```
tools/swd/swd.sh flash md-rewriter-firmware-1.1.0.bin --backup ~/md_rewriter_backups/fw-factory/fw-factory.bin --yes
```

O script confere que o chip ainda tem o firmware do backup, grava, lê de volta e compara. Termina com `written and verified`. Depois **desconecte e conecte de novo o cabo USB da placa** (o reset por software nem sempre faz a placa aparecer no USB). Teste: `mdrewriter-cli info` tem que mostrar `firmware 1.1.0`.

**Para voltar ao firmware de fábrica:** `tools/swd/swd.sh restore --backup ~/md_rewriter_backups/fw-factory/fw-factory.bin --yes`.

### 6.5 Windows (à mão)

Instale as ferramentas do ST-Link (a versão do `stlink` com `st-info.exe` e `st-flash.exe`, ou o STM32CubeProgrammer). Os comandos equivalentes: `st-info --probe`; `st-flash read backup.bin 0x08000000 0x80000` (use `0x20000` se o `st-info` disser 128 KiB); `st-flash write md-rewriter-firmware-1.1.0.bin 0x08000000`; leia de volta e compare os primeiros bytes com o arquivo. Guarde bem o backup.

---

## 7. Teste a sua unidade, passo a passo

Cada passo pode ser feito no programa gráfico (GUI) ou na linha de comando (CLI); os resultados são os mesmos. Nos exemplos de CLI a porta é achada sozinha; se houver mais de um dispositivo serial, acrescente `--port /dev/ttyACM0` (Linux) ou `--port COM3` (Windows). Os nomes dos botões estão em inglês (o programa tem inglês e português; em português: Atualizar, Conectar, Detectar cartucho, Ler ROM…, Gravar ROM…, Verificar após gravar, Ler save…, Apagar por tamanho…, Comparar arquivos…).

Faça os testes nesta ordem. Se um passo falhar, pare e leia a seção 10.

| # | Teste | GUI | CLI | O que você deve ver |
|---|-------|-----|-----|---------------------|
| 1 | Versão (sem cartucho) | **Connect** | `mdrewriter-cli info` | `Device identified.` e firmware `1.1.0` (ou `1.0` com o firmware de fábrica) |
| 2 | Slot vazio | **Detect cartridge** | `mdrewriter-cli detect` | nenhum chip flash encontrado; uma leitura do slot vazio só dá `0xFF` |
| 3 | Jogo original, só leitura | insira o cartucho, **Detect cartridge**, **Cartridge size** 2 MiB (ou o tamanho do jogo), **Read ROM…** | `mdrewriter-cli read-rom --size 2M --out jogo1.bin` | o dump é feito (cerca de 12,5 s por 2 MiB). Num cartucho de ROM de máscara o `detect` mostra flash id `0000` ou "unknown chip": **não** é cartucho flash, nunca grave nele |
| 4 | Ler duas vezes e comparar | leia o mesmo cartucho de novo para `jogo2.bin`, **Compare files…** | `mdrewriter-cli compare jogo1.bin jogo2.bin` | nenhuma diferença. Resultados diferentes indicam mau contato: limpe os pinos do cartucho e tente de novo |
| 5 | Cartucho flash: detectar | insira o cartucho flash, **Detect cartridge** | `mdrewriter-cli detect` | `S29GL064N ... Supported` e uma linha `Chip codes 017E1001 (ou 017E1000), CFI sector words ...` |
| 6 | **Faça backup do que está no cartucho flash** | **Read ROM…** 2 MiB e **Read save…** | `read-rom --size 2M --out antigo.bin`, `read-save --size 32K --out antigo.srm` | os dois arquivos existem. Guarde-os se o cartucho tiver algo que você quer |
| 7 | Apagar 2 MiB | **Erase by size…** com **Cartridge size** 2 MiB, confirme | `mdrewriter-cli erase 2M --yes` | `Erase complete.` em 6 a 9 segundos; uma nova leitura só dá `0xFF` |
| 8 | Gravar uma ROM (até 2 MiB) | **Write ROM…**, escolha um `.bin`/`.md`/`.gen`/`.smd`, deixe **Verify after writing** ligado, confirme | `mdrewriter-cli write-rom jogo.bin --yes` | `Game written`, depois `Verified: the … bytes read back match the file.` Cerca de 90 s para 2 MiB |
| 9 | Verificar de novo | **Read ROM…** e **Compare files…** com o original | `mdrewriter-cli verify jogo.bin` | idênticos |
| 10 | Dados de save | **Read save…** duas vezes, compare | `read-save --size 32K --out s1.srm`, de novo `s2.srm`, `compare s1.srm s2.srm` | arquivos idênticos |

**Não faça** os passos de apagar e gravar num cartucho que não seja flash, num cartucho que tenha um jogo do qual você não tem cópia, nem com qualquer outra coisa ligada.

### A bateria de testes que usamos (opcional, só leitura)

A bateria de bancada lê a placa e o cartucho e **nunca grava nem apaga** (não tem `--yes`). Precisa de Python 3:

```
python3 tools/hwtest/hwtest.py nocart --port ttyACM0 --expect-fw 1.1          # slot vazio
python3 tools/hwtest/hwtest.py cart   --port ttyACM0 --expect-fw 1.1 --rom-size 2M   # com cartucho
```

Ela mostra PASS / FAIL / INFO / SKIP por verificação e salva um relatório em `test-results/`. Use `--rom-size 8M` para ler também 8 MiB (nos cartuchos flash os segundos 4 MiB repetem os primeiros: normal, seção 8).

---

## 8. O limite de 2 MiB dos cartuchos flash

**Os cartuchos flash da família KY / FlashKit guardam no máximo 2 MiB de jogo, seja qual for o tamanho impresso no chip (o S29GL064N tem 8 MiB).** Testamos dois (um cartucho KY e um segundo cartucho S29GL064N) e os dois se comportam igual:

- Apagar ou gravar acima de 2 MiB falha: `erase 4M` para com `A sector did not finish erasing in time` depois de uns 23 segundos, e um jogo maior que 2 MiB não pode ser gravado.
- Uma leitura acima de 2 MiB mostra a **RAM de save** (um byte sim, outro `0xFF`) e não a flash: a placa do cartucho seleciona a flash só para os 2 MiB de baixo e a RAM para os 2 MiB de cima. Ler 8 MiB repete os mesmos 4 MiB duas vezes.

É assim que a placa do cartucho é construída (o decodificador de endereços, `docs/HWTEST.md`), não é defeito do MD Rewriter, do firmware nem do programa. Nenhuma configuração muda isso; testamos os registradores de mapeador do programador original.

**O que cabe:** quase todo jogo de Mega Drive (a maioria tem de 512 KiB a 2 MiB; o Metal Gear, de 2 MiB, foi gravado e verificado nos dois cartuchos). **O que não cabe:** os jogos maiores que 2 MiB, por exemplo o Super Street Fighter II (4 MiB, com mapeador de bancos). Para esses é preciso outro cartucho: um cartucho flash simples de 4 MiB sem esse decodificador (por exemplo o projeto aberto `MD-Flash-Cart-4MB`, MX29LV320 ou MX29L3211) ou um cartucho com mapeador de bancos.

---

## 9. O que foi testado na primeira unidade

Primeira unidade, 13/11/2026. Firmware 1.1.0-test (o mesmo programa do 1.1.0, só o texto da versão muda).

| Verificação | Resultado |
|-------------|-----------|
| Placa | microcontrolador STM32F103xE, 512 KiB de flash (os arquivos de projeto dizem uma peça de 128 KiB); o firmware 1.1 cabe |
| Backup do firmware de fábrica | lido duas vezes, idêntico, sem proteção de leitura. (O comando `restore` foi testado só no PC, com ferramentas simuladas; não foi executado na unidade) |
| Firmware 1.1 gravado por SWD | gravado e verificado; a placa aparece como `0483:5740` e fala com o programa |
| Slot vazio | duas leituras idênticas, só `0xFF` |
| Cartucho original (Rolling Thunder 2), só leitura | 2 MiB lidos duas vezes em 12,4 s, idênticos, igual ao que o firmware 1.0 deu. Nunca gravado |
| Cartucho flash S29GL064N, firmware 1.0 | grava só uma palavra sim, outra não: precisa de passadas de reparo |
| Cartucho flash S29GL064N e um cartucho KY, firmware 1.1 | reconhecidos como `S29GL064N`; apagar 2 MiB em 6 a 9 s; o Metal Gear de 2 MiB gravado e verificado **de uma vez** (cerca de 90 s com a leitura de volta); o dump é igual ao arquivo |
| Os cartuchos gravados num console de verdade | o dono testou os cartuchos gravados com este firmware num Mega Drive real (informado em 13/11/2026) |
| Gravar sobre dados sem apagar | `WRITE TIMEOUT` em 0,4 s, a placa continua respondendo: a falha segura |
| Os dois cartuchos flash têm layouts de setores diferentes com o mesmo ID de chip | por isso o firmware sempre apaga em passos pequenos, o que funciona nos dois |
| Gravar ou apagar acima de 2 MiB | impossível nesses dois cartuchos (seção 8) |

Ainda não testado: a leitura pelo mapeador de bancos SSF2, uma gravação com corte de energia, a versão com buffer de escrita e cartuchos flash de outras marcas.

---

## 10. Solução de problemas

| Sintoma | O que fazer |
|---------|-------------|
| A placa não aparece no computador | Use um cabo USB-A para USB-C com fios de dados. Tente outra porta USB. O LED POWER tem que estar aceso |
| "permission denied" (Linux) | `sudo usermod -aG dialout $USER`, faça login de novo, ou instale a regra udev |
| `probe` mostra `chip id 0x0` ou não acha nada | Confira **primeiro o GND na barra de pinos** (um pino solto é a causa usual), depois SWCLK e SWDIO, depois aperte os conectores Dupont. O script repete sozinho |
| `swd.sh` diz que o chip não tem o firmware do backup | É outra unidade, ou uma alterada. Faça um novo backup antes; nunca grave sem backup |
| Depois de gravar a placa não aparece no USB | Desconecte e conecte de novo o cabo USB da placa |
| `detect` diz chip desconhecido | Não é cartucho flash (é um original), ou o chip não está na tabela: não grave |
| "did not finish erasing" | O cartucho não é flash, está danificado, ou você pediu mais de 2 MiB (seção 8) |
| `WRITE TIMEOUT` | A área não foi apagada antes (apague, depois grave), ou o chip está danificado |
| A verificação falha | Limpe os contatos do cartucho, use outro cabo USB, repita o apagamento e a gravação |
| Duas leituras do mesmo cartucho diferem | Mau contato: limpe os pinos com álcool isopropílico e insira de novo |
| Um jogo gravado não inicia no console | Confira se o arquivo é uma ROM válida (o programa avisa de problemas no cabeçalho), se tem no máximo 2 MiB e se a verificação passou |

Se encontrar algo que não está aqui, abra uma issue com o registro do programa e a saída de `mdrewriter-cli detect`.
