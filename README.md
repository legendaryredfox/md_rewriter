# md_rewriter

**Idiomas / Языки / 语言 / Languages:** [Português (Brasil)](#português-brasil) · [Русский](#русский) · [简体中文](#简体中文) · [English](#english)

---

<a id="português-brasil"></a>
# Português (Brasil)

Ferramenta portátil para cartuchos de Sega Mega Drive: faz backup e restauração de ROMs e de jogos salvos (saves) no seu computador e grava ROMs em cartuchos flash regraváveis de terceiros.

## Componentes

| Pasta | Conteúdo |
|-------|----------|
| `legacy_firmware/` | Código-fonte do firmware do microcontrolador (STM32F103VB, projeto Keil). O firmware compilado está na página de releases: [Release 1.0](https://github.com/Epicjoy/md_rewriter/releases/download/1.0/md-rewriter-firmware-1.0.hex). |
| `Hardware/` | BOM, Gerber e arquivos da PCB. Você pode fabricar a placa conforme sua necessidade. |
| `Software/` | Aplicativo de computador (Qt 6). Funciona em **Linux** e Windows. |
| `docs/` | Especificação e plano das correções, e notas técnicas. |

Loja oficial da Epicjoy no AliExpress: [comprar](https://www.aliexpress.us/item/3256809599500641.html?pdp_npi=4%40dis%21USD%21US%20%2428.79%21US%20%2412.59%21%21%21204.30%2189.34%21%402103244817599933506882056ef13b%2112000050402273301%21sh%21US%212937514916%21X&spm=a2g0o.store_pc_allItems_or_groupList.new_all_items_2007603309925.1005009785815393&gatewayAdapt=glo2usa)

Gostou do projeto? Apoie: [Buy Me a Coffee](https://buymeacoffee.com/legendaryredfox)

## Recursos

- Sem restrição de região: cartuchos dos EUA, do Japão e da Europa.
- Faz backup de jogos de cartuchos de Mega Drive para o computador e grava jogos em cartuchos flash regraváveis de terceiros.
- Lê e grava os dados de save (SRAM) do cartucho.
- Compara dois arquivos byte a byte (por exemplo, uma leitura e a ROM original), sem precisar do dispositivo.
- Grava ROMs de qualquer tamanho par até 4 MiB, converte arquivos `.smd` e verifica a gravação lendo o cartucho de volta.
- Interface que segue o tema do sistema (claro ou escuro), com confirmação antes de qualquer operação destrutiva e botão Cancelar para leituras e gravações.
- Guia do usuário embutido (menu **Ajuda → Guia do usuário** ou **F1**): receitas de uso, o que cada mensagem do registro significa e solução de problemas, com busca. Disponível em português e inglês.
- Código aberto.

![Janela principal](docs/images/ui-main.png)

## Instalação do aplicativo

### Linux (testado no Linux Mint 22 com Qt 6.4)

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cd Software
qmake6 MDRewriter.pro
make -j"$(nproc)"
./MDRewriter                 # executar sem instalar
sudo make install            # opcional: instala em /usr/local (programa, ícone, atalho e regra udev)
```

**Permissão para acessar a porta serial.** O dispositivo aparece como `/dev/ttyACM0` (ou similar). Escolha **uma** das opções:

1. Adicione seu usuário ao grupo `dialout` e faça login novamente: `sudo usermod -aG dialout $USER`
2. Ou instale a regra udev incluída, que também impede o ModemManager de usar a porta:
   ```bash
   sudo cp Software/linux/99-md-rewriter.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
   Depois reconecte o cabo USB.

### Windows

Baixe o programa pronto na página de releases, ou abra `Software/MDRewriter.pro` no Qt Creator e compile. O Windows reconhece o dispositivo como uma porta COM (instale o driver "STM32 Virtual ComPort" se necessário).

### Gerar pacotes (.deb, Windows, macOS)

Só o pacote Linux foi construído e testado nesta máquina. As etapas de Windows e macOS abaixo seguem a documentação do Qt, mas **não foram executadas** neste projeto; se algo falhar, abra uma issue.

**Linux (.deb).** O script usa a versão de `Software/MDRewriter.pro`, faz o build de release com prefixo `/usr` e inclui programa, atalho, ícone, regra udev e licença:

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools dpkg-dev fakeroot
tools/package-deb.sh                        # build/dist/mdrewriter_<version>_amd64.deb + .sha256
sudo apt install ./build/dist/mdrewriter_*_amd64.deb
```

**Windows (.zip e .msi).** Instale o Qt 6.4 ou mais novo pelo instalador oficial (kit MinGW 64-bit e o módulo Qt Serial Port) e abra o prompt "Qt 6.x (MinGW 64-bit)". O `windeployqt` copia as DLLs do Qt para a pasta, que então vira um `.zip` portátil:

```bat
rem "Qt 6.x (MinGW 64-bit)" prompt from the Qt installer (Qt 6.4 or newer, with Qt Serial Port)
mkdir build & cd build
qmake ..\Software\MDRewriter.pro
mingw32-make release
mkdir MDRewriter & copy release\MDRewriter.exe MDRewriter\
windeployqt --release MDRewriter\MDRewriter.exe
powershell Compress-Archive MDRewriter MDRewriter-windows-x64.zip
```

Para um `.msi`, empacote essa pasta com o [WiX Toolset](https://wixtoolset.org/) v4 ou mais novo (um arquivo `MDRewriter.wxs` que lista `MDRewriter.exe`, as DLLs e `platforms\`, `styles\` etc.). Este repositório ainda não inclui o `.wxs`. (O Inno Setup ou o NSIS também servem se você prefere um instalador `.exe`.) Com o WiX instalado:

```bat
dotnet tool install --global wix
wix build MDRewriter.wxs -o MDRewriter.msi
```

**macOS (.dmg).** Instale o Qt 6 (por exemplo com o Homebrew). O `qmake` gera `MDRewriter.app` e o `macdeployqt` embute o Qt e cria o `.dmg`. O app sai sem assinatura: o Gatekeeper pede "Abrir" com o botão direito, e assinar e notarizar exige uma conta de desenvolvedor Apple (`macdeployqt -codesign=...`). A porta aparece como `/dev/cu.usbmodem*`. Ainda não há ícone `.icns` no projeto.

```bash
brew install qt                              # Qt 6 with Qt Serial Port
mkdir build && cd build
"$(brew --prefix qt)/bin/qmake" ../Software/MDRewriter.pro CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"
"$(brew --prefix qt)/bin/macdeployqt" MDRewriter.app -dmg      # MDRewriter.dmg
```

## Como usar

1. **Conecte o MD Rewriter** ao computador por USB e **insira o cartucho** no dispositivo.
2. Abra o **MD Rewriter**. A lista de portas se atualiza sozinha; a porta do dispositivo (USB `0483:5740`, "STM32 Virtual ComPort") é escolhida automaticamente. Se não aparecer, clique em **Atualizar**.
3. Clique em **Conectar**. O registro mostra "Device identified." e os botões de ação são habilitados. Os botões ficam desabilitados enquanto não houver conexão ou durante uma operação.
4. Clique em **Detectar cartucho** para identificar o chip flash (mostra o ID do chip e o modelo, quando conhecido). O firmware 1.0 só reconhece o chip MX29LV640EB e mostra `NO FIND NGP CARD` para os demais, mas isso não impede nada: o programa nomeia outros chips pelo ID (por exemplo ST M29W640D) e explica o resultado.

### Fazer backup de um jogo (ler a ROM)

1. Em **Jogo (ROM)**, escolha o **Tamanho do cartucho** (512 KiB, 1 MiB, 2 MiB ou 4 MiB).
2. Clique em **Ler ROM…** e escolha onde salvar o arquivo (`.bin`).
3. Acompanhe a barra de progresso. O arquivo só é criado quando a leitura termina completa e sem erros. Se você cancelar ou ocorrer uma falha, **nenhum arquivo parcial** é deixado.

### Gravar um jogo em um cartucho flash

1. **Apague o cartucho antes** se ele não estiver vazio (veja abaixo).
2. Clique em **Gravar ROM…** e escolha o arquivo (`.bin`, `.md`, `.gen` ou `.smd`). Qualquer ROM de tamanho par entre 512 bytes e 4 MiB é aceita (256 KiB, 768 KiB, 1,5 MiB etc.): ela é gravada como está e o resto do chip fica em branco. Arquivos `.smd` são convertidos para ROM simples. O programa avisa, sem bloquear, se o cabeçalho parecer errado (sem `SEGA`, checksum ruim, arquivo truncado).
3. Confirme o aviso. A barra mostra o progresso; **Cancelar** interrompe, mas deixa o cartucho com uma imagem parcial.
4. Com **Verificar após gravar** marcado (padrão), o programa lê o cartucho de volta e compara com o arquivo; o registro mostra "Verified" ou o primeiro deslocamento que difere.

### Apagar o cartucho

- **Apagar por tamanho…**: apaga apenas os setores flash do tamanho escolhido em "Tamanho do cartucho". Mais rápido.
- **Apagar tudo…**: apaga o chip flash inteiro e a memória de save. Pode levar vários minutos e **não pode ser cancelado**. Não desconecte o dispositivo durante o processo.

Os dois pedem confirmação, pois **destroem os dados do cartucho**.

### Dados de save (SRAM)

- Escolha o **Tamanho do save** (32 KiB ou 8 KiB) e use **Ler save…** para copiar os saves do cartucho para um arquivo (`.srm`).
- **Gravar save…** substitui os saves do cartucho pelo conteúdo de um arquivo de 8 KiB ou 32 KiB (pede confirmação).

### Comparar arquivos

**Comparar arquivos…** pergunta por dois arquivos e lista as primeiras 20 diferenças (posição do byte). Serve para conferir se uma leitura é idêntica à ROM original. Não precisa do dispositivo.

### Solução de problemas

| Sintoma | O que fazer |
|---------|-------------|
| "permission denied" ao conectar (Linux) | Grupo `dialout` ou regra udev (seção de instalação). |
| A porta não aparece | Troque o cabo (use um com dados, não só de carga), reconecte e clique em **Atualizar**. |
| "the device did not answer" | A porta está errada ou o firmware não está gravado no dispositivo. |
| Leitura "incompleta ou fora de sincronia" | Use outro cabo/porta USB e tente de novo; o arquivo não é salvo nesse caso. |
| "The cartridge did not finish erasing in time" | O cartucho pode estar com defeito ou não ser flash (requer firmware que envie esse aviso; o firmware novo envia). |
| Usar uma porta que o sistema não lista | Defina `MD_REWRITER_EXTRA_PORT=/caminho/da/porta` antes de abrir o programa. |

## Desenvolvimento e testes

```bash
cd Software/tests
qmake6 tests.pro && make -j"$(nproc)" && make check     # unitários, integração e ponta a ponta
cd ../.. && tools/check-firmware.sh                      # verificação estática do firmware (arm-none-eabi-gcc, cppcheck)
```

- `tst_protocol`: unitários (comandos, tabelas de tamanhos, analisador de fluxo, comparador).
- `tst_session`: integração (sessão do dispositivo contra um firmware simulado, com respostas fragmentadas, falhas e cancelamento).
- `tst_help`: guia embutido (estrutura, idiomas, links) e verificação de que as mensagens e teclas citadas no guia existem no programa.
- `tst_e2e`: ponta a ponta (janela real + porta serial real em um pseudo-terminal + firmware simulado; apenas Linux). Defina `QT_QPA_PLATFORM=offscreen` para rodar sem tela.

O firmware não pôde ser testado em hardware real nestas alterações: veja `docs/SPEC.md` e `docs/PLAN.md`.

## Licença

Licença [MIT](LICENSE). O código de terceiros em `legacy_firmware/Drivers` e `legacy_firmware/Middlewares` (STMicroelectronics, ARM) mantém as licenças originais.

---

<a id="русский"></a>
# Русский

Портативный инструмент для картриджей Sega Mega Drive: резервное копирование и восстановление ROM и сохранений на вашем компьютере, а также запись ROM на сторонние перезаписываемые flash-картриджи.

## Состав проекта

| Папка | Содержимое |
|-------|------------|
| `legacy_firmware/` | Исходный код прошивки микроконтроллера (STM32F103VB, проект Keil). Готовая прошивка — в разделе релизов: [Release 1.0](https://github.com/Epicjoy/md_rewriter/releases/download/1.0/md-rewriter-firmware-1.0.hex). |
| `Hardware/` | BOM, Gerber и файлы печатной платы. Плату можно изготовить самостоятельно. |
| `Software/` | Программа для компьютера (Qt 6). Работает в **Linux** и Windows. |
| `docs/` | Спецификация и план исправлений, технические заметки. |

Официальный магазин Epicjoy на AliExpress: [купить](https://www.aliexpress.us/item/3256809599500641.html?pdp_npi=4%40dis%21USD%21US%20%2428.79%21US%20%2412.59%21%21%21204.30%2189.34%21%402103244817599933506882056ef13b%2112000050402273301%21sh%21US%212937514916%21X&spm=a2g0o.store_pc_allItems_or_groupList.new_all_items_2007603309925.1005009785815393&gatewayAdapt=glo2usa)

Нравится проект? Поддержите: [Buy Me a Coffee](https://buymeacoffee.com/legendaryredfox)

## Возможности

- Без региональных ограничений: картриджи США, Японии и Европы.
- Резервное копирование игр с картриджей Mega Drive на компьютер и запись игр на сторонние перезаписываемые картриджи.
- Чтение и запись сохранений (SRAM) картриджа.
- Побайтовое сравнение двух файлов (например, дампа и оригинальной ROM) — устройство не нужно.
- Записывает ROM любого чётного размера до 4 MiB, преобразует файлы `.smd` и проверяет запись обратным чтением картриджа.
- Интерфейс использует системную тему (светлую или тёмную), запрашивает подтверждение перед любой разрушающей операцией, а чтение и запись можно отменить кнопкой «Cancel».
- Встроенное руководство (меню **Help → User guide** или **F1**): сценарии использования, значение сообщений журнала и устранение неполадок, с поиском. Доступно на португальском и английском.
- Открытый исходный код.

![Главное окно](docs/images/ui-main.png)

## Установка программы

### Linux (проверено на Linux Mint 22 с Qt 6.4)

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cd Software
qmake6 MDRewriter.pro
make -j"$(nproc)"
./MDRewriter                 # запуск без установки
sudo make install            # по желанию: установка в /usr/local (программа, значок, ярлык и правило udev)
```

**Доступ к последовательному порту.** Устройство появляется как `/dev/ttyACM0` (или аналогичное). Выберите **один** из вариантов:

1. Добавьте пользователя в группу `dialout` и заново войдите в систему: `sudo usermod -aG dialout $USER`
2. Либо установите входящее в проект правило udev; оно же не даёт ModemManager занимать порт:
   ```bash
   sudo cp Software/linux/99-md-rewriter.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
   После этого переподключите USB-кабель.

### Windows

Скачайте готовую программу в разделе релизов или откройте `Software/MDRewriter.pro` в Qt Creator и соберите проект. Windows определяет устройство как COM-порт (при необходимости установите драйвер «STM32 Virtual ComPort»).

### Сборка пакетов (.deb, Windows, macOS)

На машине автора собран и проверен только пакет для Linux. Шаги для Windows и macOS ниже следуют документации Qt, но для этого проекта **не выполнялись**; если что-то не работает, создайте issue.

**Linux (.deb).** Скрипт берёт версию из `Software/MDRewriter.pro`, делает release-сборку с префиксом `/usr` и упаковывает программу, ярлык, иконку, правило udev и лицензию:

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools dpkg-dev fakeroot
tools/package-deb.sh                        # build/dist/mdrewriter_<version>_amd64.deb + .sha256
sudo apt install ./build/dist/mdrewriter_*_amd64.deb
```

**Windows (.zip и .msi).** Установите Qt 6.4 или новее официальным установщиком (комплект MinGW 64-bit и модуль Qt Serial Port) и откройте консоль «Qt 6.x (MinGW 64-bit)». `windeployqt` копирует DLL Qt рядом с программой, а папка превращается в переносимый `.zip`:

```bat
rem "Qt 6.x (MinGW 64-bit)" prompt from the Qt installer (Qt 6.4 or newer, with Qt Serial Port)
mkdir build & cd build
qmake ..\Software\MDRewriter.pro
mingw32-make release
mkdir MDRewriter & copy release\MDRewriter.exe MDRewriter\
windeployqt --release MDRewriter\MDRewriter.exe
powershell Compress-Archive MDRewriter MDRewriter-windows-x64.zip
```

Для `.msi` упакуйте эту папку с помощью [WiX Toolset](https://wixtoolset.org/) v4 или новее (файл `MDRewriter.wxs`, перечисляющий `MDRewriter.exe`, DLL и каталоги `platforms\`, `styles\` и т. д.). Файла `.wxs` в репозитории пока нет. (Подойдут и Inno Setup или NSIS, если нужен установщик `.exe`.) Когда WiX установлен:

```bat
dotnet tool install --global wix
wix build MDRewriter.wxs -o MDRewriter.msi
```

**macOS (.dmg).** Установите Qt 6 (например, через Homebrew). `qmake` создаёт `MDRewriter.app`, а `macdeployqt` встраивает Qt и создаёт `.dmg`. Приложение не подписано: Gatekeeper потребует открыть его правой кнопкой («Открыть»), а подпись и нотаризация требуют аккаунта разработчика Apple (`macdeployqt -codesign=...`). Порт виден как `/dev/cu.usbmodem*`. Иконки `.icns` в проекте пока нет.

```bash
brew install qt                              # Qt 6 with Qt Serial Port
mkdir build && cd build
"$(brew --prefix qt)/bin/qmake" ../Software/MDRewriter.pro CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"
"$(brew --prefix qt)/bin/macdeployqt" MDRewriter.app -dmg      # MDRewriter.dmg
```

## Как пользоваться

1. **Подключите MD Rewriter** к компьютеру по USB и **вставьте картридж** в устройство.
2. Запустите **MD Rewriter**. Список портов обновляется сам; порт устройства (USB `0483:5740`, «STM32 Virtual ComPort») выбирается автоматически. Если порта нет, нажмите **Refresh**.
3. Нажмите **Connect**. В журнале появится «Device identified.», и кнопки действий станут доступны. Без соединения и во время операции кнопки неактивны.
4. Нажмите **Detect cartridge**, чтобы определить flash-микросхему (показываются её ID и модель, если она известна). Прошивка 1.0 распознаёт только MX29LV640EB и для любой другой микросхемы пишет `NO FIND NGP CARD`, но это ничего не блокирует: программа называет другие микросхемы по ID (например, ST M29W640D) и объясняет результат.

### Резервная копия игры (чтение ROM)

1. В группе **Game (ROM)** выберите **Cartridge size** (512 KiB, 1 MiB, 2 MiB или 4 MiB).
2. Нажмите **Read ROM…** и выберите, куда сохранить файл (`.bin`).
3. Следите за индикатором выполнения. Файл создаётся только после полного и безошибочного чтения. При отмене или сбое **неполный файл не остаётся**.

### Запись игры на flash-картридж

1. Если картридж не пуст, **сначала сотрите его** (см. ниже).
2. Нажмите **Write ROM…** и выберите файл (`.bin`, `.md`, `.gen` или `.smd`). Принимается любой ROM чётного размера от 512 байт до 4 MiB (256 KiB, 768 KiB, 1,5 MiB и т. д.): он записывается как есть, остальная часть микросхемы остаётся чистой. Файлы `.smd` автоматически преобразуются в обычный ROM. Если заголовок выглядит неверно (нет `SEGA`, неверная контрольная сумма, файл обрезан), программа предупреждает, но запись не блокирует. С включённой опцией **Verify after writing** (по умолчанию) программа после записи читает картридж обратно и сравнивает с файлом.
3. Подтвердите предупреждение. **Cancel** прервёт запись, но на картридже останется неполный образ.

### Стирание картриджа

- **Erase by size…** — стирает только flash-секторы выбранного в «Cartridge size» объёма. Быстрее.
- **Erase all…** — стирает всю flash-микросхему и память сохранений. Может занять несколько минут и **не может быть отменено**. Не отключайте устройство во время стирания.

Обе операции требуют подтверждения, так как **уничтожают данные на картридже**.

### Сохранения (SRAM)

- Выберите **Save size** (32 KiB или 8 KiB) и нажмите **Read save…**, чтобы скопировать сохранения с картриджа в файл (`.srm`).
- **Write save…** заменяет сохранения на картридже содержимым файла размером 8 KiB или 32 KiB (с подтверждением).

### Сравнение файлов

**Compare files…** запрашивает два файла и показывает первые 20 различий (смещение байта). Удобно, чтобы убедиться, что дамп совпадает с оригинальной ROM. Устройство не требуется.

### Устранение неполадок

| Симптом | Что делать |
|---------|-----------|
| «permission denied» при подключении (Linux) | Группа `dialout` или правило udev (раздел установки). |
| Порт не появляется | Смените кабель (нужен кабель с данными, а не только зарядный), переподключите и нажмите **Refresh**. |
| «the device did not answer» | Выбран не тот порт, или в устройство не записана прошивка. |
| Чтение «incomplete or out of sync» | Попробуйте другой кабель/USB-порт ещё раз; файл в этом случае не сохраняется. |
| Порт, которого нет в списке системы | Задайте `MD_REWRITER_EXTRA_PORT=/путь/к/порту` перед запуском. |

## Разработка и тесты

```bash
cd Software/tests
qmake6 tests.pro && make -j"$(nproc)" && make check     # модульные, интеграционные и сквозные тесты
cd ../.. && tools/check-firmware.sh                      # статическая проверка прошивки (arm-none-eabi-gcc, cppcheck)
```

- `tst_protocol` — модульные тесты (команды, таблицы размеров, разбор потока, сравнение файлов).
- `tst_session` — интеграционные тесты (сессия устройства с имитацией прошивки: фрагментация ответов, сбои, отмена).
- `tst_help` — встроенное руководство (структура, языки, ссылки) и проверка того, что сообщения и клавиши из руководства есть в программе.
- `tst_e2e` — сквозные тесты (настоящее окно + настоящий последовательный порт на псевдотерминале + имитация прошивки; только Linux). Для запуска без экрана задайте `QT_QPA_PLATFORM=offscreen`.

Изменения прошивки не проверялись на реальном оборудовании: см. `docs/SPEC.md` и `docs/PLAN.md`.

## Лицензия

Лицензия [MIT](LICENSE). Сторонний код в `legacy_firmware/Drivers` и `legacy_firmware/Middlewares` (STMicroelectronics, ARM) сохраняет исходные лицензии.

---

<a id="简体中文"></a>
# 简体中文

便携式 Sega Mega Drive（世嘉 MD）卡带工具：可在电脑上备份和恢复游戏 ROM 与存档，并把 ROM 写入第三方可重复擦写的 Flash 卡带。

## 项目组成

| 目录 | 内容 |
|------|------|
| `legacy_firmware/` | 单片机固件源码（STM32F103VB，Keil 工程）。编译好的固件见发布页：[Release 1.0](https://github.com/Epicjoy/md_rewriter/releases/download/1.0/md-rewriter-firmware-1.0.hex)。 |
| `Hardware/` | BOM、Gerber 和 PCB 文件，可按需自行制板。 |
| `Software/` | 上位机软件（Qt 6），支持 **Linux** 和 Windows。 |
| `docs/` | 修复规格说明与计划、技术说明。 |

Epicjoy 官方速卖通店铺：[购买](https://www.aliexpress.us/item/3256809599500641.html?pdp_npi=4%40dis%21USD%21US%20%2428.79%21US%20%2412.59%21%21%21204.30%2189.34%21%402103244817599933506882056ef13b%2112000050402273301%21sh%21US%212937514916%21X&spm=a2g0o.store_pc_allItems_or_groupList.new_all_items_2007603309925.1005009785815393&gatewayAdapt=glo2usa)

喜欢这个项目？欢迎支持：[Buy Me a Coffee](https://buymeacoffee.com/legendaryredfox)

## 功能

- 无区域限制，兼容美版、日版和欧版卡带。
- 将 MD 卡带中的游戏备份到电脑，或写入第三方可重复擦写的卡带。
- 读取并备份卡带存档（SRAM），方便管理。
- 逐字节比较两个文件（例如读出的文件与原始 ROM），无需连接设备。
- 可写入最大 4 MiB 的任意偶数大小 ROM，自动转换 `.smd` 文件，并在写入后把卡带读回进行校验。
- 界面跟随系统主题（浅色或深色）；任何破坏性操作前都会确认，读写过程可点击“Cancel”取消。
- 内置用户指南（菜单 **Help → User guide** 或按 **F1**）：使用场景、日志信息含义和故障排除，支持搜索。提供葡萄牙语和英语版本。
- 开源。

![主窗口](docs/images/ui-main.png)

## 安装软件

### Linux（已在 Linux Mint 22、Qt 6.4 上测试）

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cd Software
qmake6 MDRewriter.pro
make -j"$(nproc)"
./MDRewriter                 # 不安装直接运行
sudo make install            # 可选：安装到 /usr/local（程序、图标、启动项和 udev 规则）
```

**串口权限。** 设备会显示为 `/dev/ttyACM0`（或类似名称）。请任选**一种**方式：

1. 把当前用户加入 `dialout` 组并重新登录：`sudo usermod -aG dialout $USER`
2. 或安装随附的 udev 规则（同时可防止 ModemManager 占用该串口）：
   ```bash
   sudo cp Software/linux/99-md-rewriter.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
   然后重新插拔 USB 线。

### Windows

在发布页下载编译好的程序，或用 Qt Creator 打开 `Software/MDRewriter.pro` 自行编译。Windows 会把设备识别为 COM 口（必要时安装“STM32 Virtual ComPort”驱动）。

### 构建安装包（.deb、Windows、macOS）

只有 Linux 安装包在维护者的机器上构建并测试过。下面的 Windows 和 macOS 步骤遵循 Qt 文档，但本项目**尚未实际运行**；如果失败，请提交 issue。

**Linux（.deb）。** 脚本从 `Software/MDRewriter.pro` 读取版本号，以前缀 `/usr` 做 release 构建，并打包程序、桌面入口、图标、udev 规则和许可证：

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools dpkg-dev fakeroot
tools/package-deb.sh                        # build/dist/mdrewriter_<version>_amd64.deb + .sha256
sudo apt install ./build/dist/mdrewriter_*_amd64.deb
```

**Windows（.zip 和 .msi）。** 用官方安装器安装 Qt 6.4 或更新版本（MinGW 64-bit 套件和 Qt Serial Port 模块），并打开“Qt 6.x (MinGW 64-bit)”命令行。`windeployqt` 会把 Qt 的 DLL 复制到程序旁边，该文件夹即可压缩为便携的 `.zip`：

```bat
rem "Qt 6.x (MinGW 64-bit)" prompt from the Qt installer (Qt 6.4 or newer, with Qt Serial Port)
mkdir build & cd build
qmake ..\Software\MDRewriter.pro
mingw32-make release
mkdir MDRewriter & copy release\MDRewriter.exe MDRewriter\
windeployqt --release MDRewriter\MDRewriter.exe
powershell Compress-Archive MDRewriter MDRewriter-windows-x64.zip
```

要生成 `.msi`，请用 [WiX Toolset](https://wixtoolset.org/) v4 或更新版本打包该文件夹（一个列出 `MDRewriter.exe`、各 DLL 以及 `platforms\`、`styles\` 等目录的 `MDRewriter.wxs`）。仓库里暂时没有 `.wxs`。（如果想要 `.exe` 安装程序，也可以用 Inno Setup 或 NSIS。）安装好 WiX 后：

```bat
dotnet tool install --global wix
wix build MDRewriter.wxs -o MDRewriter.msi
```

**macOS（.dmg）。** 安装 Qt 6（例如通过 Homebrew）。`qmake` 生成 `MDRewriter.app`，`macdeployqt` 嵌入 Qt 并创建 `.dmg`。应用未签名：Gatekeeper 会要求右键选择“打开”；签名和公证需要 Apple 开发者账号（`macdeployqt -codesign=...`）。端口显示为 `/dev/cu.usbmodem*`。项目里还没有 `.icns` 图标。

```bash
brew install qt                              # Qt 6 with Qt Serial Port
mkdir build && cd build
"$(brew --prefix qt)/bin/qmake" ../Software/MDRewriter.pro CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"
"$(brew --prefix qt)/bin/macdeployqt" MDRewriter.app -dmg      # MDRewriter.dmg
```

## 使用方法

1. 用 USB 线把 **MD Rewriter 连接到电脑**，并把**卡带插入**设备。
2. 打开 **MD Rewriter**。端口列表会自动刷新，设备端口（USB `0483:5740`，“STM32 Virtual ComPort”）会被自动选中。如果没有出现，请点击 **Refresh**。
3. 点击 **Connect**。日志中出现“Device identified.”后，操作按钮即可使用。未连接或操作进行中时，按钮为灰色。
4. 点击 **Detect cartridge** 识别 Flash 芯片（显示芯片 ID，已知时还会显示型号）。固件 1.0 只认识 MX29LV640EB，其他芯片都会显示 `NO FIND NGP CARD`，但这不会阻止任何操作：程序会根据 ID 给出其他芯片的名称（例如 ST M29W640D）并解释结果。

### 备份游戏（读取 ROM）

1. 在 **Game (ROM)** 中选择 **Cartridge size**（512 KiB、1 MiB、2 MiB 或 4 MiB）。
2. 点击 **Read ROM…**，选择保存位置（`.bin` 文件）。
3. 观察进度条。只有读取完整且无错误时才会生成文件；若取消或出错，**不会留下不完整的文件**。

### 向 Flash 卡带写入游戏

1. 如果卡带不是空白的，请**先擦除**（见下文）。
2. 点击 **Write ROM…** 并选择文件（`.bin`、`.md`、`.gen` 或 `.smd`）。支持 512 字节到 4 MiB 之间任何偶数大小的 ROM（256 KiB、768 KiB、1.5 MiB 等）：按原样写入，芯片其余部分保持空白。`.smd` 文件会自动转换为普通 ROM。如果文件头看起来不对（没有 `SEGA`、校验和错误、文件被截断），程序会提醒但不会阻止写入。勾选 **Verify after writing**（默认开启）后，写入结束时程序会把卡带读回并与文件比较。
3. 确认提示。写入时可点 **Cancel** 中断，但卡带上会留下不完整的镜像。

### 擦除卡带

- **Erase by size…**：只擦除“Cartridge size”所选容量对应的 Flash 扇区，速度较快。
- **Erase all…**：擦除整个 Flash 芯片和存档存储器。可能需要几分钟，且**无法取消**。擦除期间请勿拔出设备。

两者都会要求确认，因为它们会**销毁卡带上的数据**。

### 存档（SRAM）

- 选择 **Save size**（32 KiB 或 8 KiB），点击 **Read save…** 把卡带存档保存为文件（`.srm`）。
- **Write save…** 用一个 8 KiB 或 32 KiB 的文件替换卡带上的存档（需确认）。

### 比较文件

**Compare files…** 会让你选择两个文件，并列出前 20 处差异（字节偏移）。可用来确认读出的文件与原始 ROM 完全一致，无需连接设备。

### 常见问题

| 现象 | 处理办法 |
|------|----------|
| 连接时提示 “permission denied”（Linux） | 使用 `dialout` 组或 udev 规则（见安装部分）。 |
| 端口不出现 | 更换数据线（需支持数据传输，不能只是充电线），重新插拔并点击 **Refresh**。 |
| “the device did not answer” | 端口选错，或设备中没有烧录固件。 |
| 读取提示 “incomplete or out of sync” | 换一根线或 USB 口后重试；此时文件不会被保存。 |
| 系统未列出的端口 | 启动前设置 `MD_REWRITER_EXTRA_PORT=/端口/路径`。 |

## 开发与测试

```bash
cd Software/tests
qmake6 tests.pro && make -j"$(nproc)" && make check     # 单元测试、集成测试和端到端测试
cd ../.. && tools/check-firmware.sh                      # 固件静态检查（arm-none-eabi-gcc、cppcheck）
```

- `tst_protocol`：单元测试（命令、容量表、数据流解析器、文件比较）。
- `tst_session`：集成测试（设备会话对接模拟固件，含分片应答、故障和取消）。
- `tst_help`：内置指南（结构、语言、链接），并检查指南中引用的信息和快捷键确实存在于程序中。
- `tst_e2e`：端到端测试（真实窗口 + 伪终端上的真实串口 + 模拟固件；仅限 Linux）。设置 `QT_QPA_PLATFORM=offscreen` 可无显示运行。

固件的改动尚未在真实硬件上测试，详见 `docs/SPEC.md` 和 `docs/PLAN.md`。

## 许可证

采用 [MIT](LICENSE) 许可证。`legacy_firmware/Drivers` 和 `legacy_firmware/Middlewares` 中的第三方代码（STMicroelectronics、ARM）保留其原有许可证。

---

<a id="english"></a>
# English

A portable Sega Mega Drive cartridge tool: back up and restore game ROMs and save games to your computer, and write ROMs to third-party rewritable flash cartridges.

## Components

| Folder | Contents |
|--------|----------|
| `legacy_firmware/` | Source code of the microcontroller firmware (STM32F103VB, Keil project). The compiled firmware is in the releases section: [Release 1.0](https://github.com/Epicjoy/md_rewriter/releases/download/1.0/md-rewriter-firmware-1.0.hex). |
| `Hardware/` | BOM, Gerber and PCB files. You can build the board to suit your needs. |
| `Software/` | Desktop application (Qt 6). Runs on **Linux** and Windows. |
| `docs/` | Specification and plan of the fixes, technical notes. |

Epicjoy official AliExpress store: [Buy now](https://www.aliexpress.us/item/3256809599500641.html?pdp_npi=4%40dis%21USD%21US%20%2428.79%21US%20%2412.59%21%21%21204.30%2189.34%21%402103244817599933506882056ef13b%2112000050402273301%21sh%21US%212937514916%21X&spm=a2g0o.store_pc_allItems_or_groupList.new_all_items_2007603309925.1005009785815393&gatewayAdapt=glo2usa)

Like the project? Support it: [Buy Me a Coffee](https://buymeacoffee.com/legendaryredfox)

## Features

- No region restrictions: US, Japanese and European cartridges.
- Back up MD cartridge games to your PC, or write games to third-party rewritable cartridges.
- Read and back up the cartridge save data (SRAM).
- Compare two files byte by byte (for example a dump and the original ROM), no device needed.
- Writes ROMs of any even size up to 4 MiB, converts `.smd` files and verifies the write by reading the cartridge back.
- Follows the system theme (light or dark), asks for confirmation before any destructive operation, and has a Cancel button for reads and writes.
- Built-in user guide (menu **Help → User guide** or **F1**): usage recipes, what each log message means and troubleshooting, with search. Available in Portuguese and English.
- Open source.

![Main window](docs/images/ui-main.png)

## Installing the application

### Linux (tested on Linux Mint 22 with Qt 6.4)

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cd Software
qmake6 MDRewriter.pro
make -j"$(nproc)"
./MDRewriter                 # run without installing
sudo make install            # optional: installs to /usr/local (program, icon, launcher and udev rule)
```

**Permission to use the serial port.** The device shows up as `/dev/ttyACM0` (or similar). Choose **one** of these:

1. Add your user to the `dialout` group and log in again: `sudo usermod -aG dialout $USER`
2. Or install the bundled udev rule, which also keeps ModemManager away from the port:
   ```bash
   sudo cp Software/linux/99-md-rewriter.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
   Then replug the USB cable.

### Windows

Download the ready-made program from the releases page, or open `Software/MDRewriter.pro` in Qt Creator and build it. Windows exposes the device as a COM port (install the "STM32 Virtual ComPort" driver if needed).

### Building packages (.deb, Windows, macOS)

Only the Linux package was built and tested on the maintainer's machine. The Windows and macOS steps below follow the Qt documentation but have **not been run** for this project; open an issue if one fails.

**Linux (.deb).** The script takes the version from `Software/MDRewriter.pro`, makes a release build with prefix `/usr` and packs the program, desktop entry, icon, udev rule and license:

```bash
sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools dpkg-dev fakeroot
tools/package-deb.sh                        # build/dist/mdrewriter_<version>_amd64.deb + .sha256
sudo apt install ./build/dist/mdrewriter_*_amd64.deb
```

**Windows (.zip and .msi).** Install Qt 6.4 or newer with the official installer (MinGW 64-bit kit and the Qt Serial Port module) and open the "Qt 6.x (MinGW 64-bit)" prompt. `windeployqt` copies the Qt DLLs next to the program, and the folder becomes a portable `.zip`:

```bat
rem "Qt 6.x (MinGW 64-bit)" prompt from the Qt installer (Qt 6.4 or newer, with Qt Serial Port)
mkdir build & cd build
qmake ..\Software\MDRewriter.pro
mingw32-make release
mkdir MDRewriter & copy release\MDRewriter.exe MDRewriter\
windeployqt --release MDRewriter\MDRewriter.exe
powershell Compress-Archive MDRewriter MDRewriter-windows-x64.zip
```

For an `.msi`, package that folder with the [WiX Toolset](https://wixtoolset.org/) v4 or newer (a `MDRewriter.wxs` that lists `MDRewriter.exe`, the DLLs and `platforms\`, `styles\` and so on). This repository does not include the `.wxs` yet. (Inno Setup or NSIS work too if you prefer an `.exe` installer.) With WiX installed:

```bat
dotnet tool install --global wix
wix build MDRewriter.wxs -o MDRewriter.msi
```

**macOS (.dmg).** Install Qt 6 (for example with Homebrew). `qmake` produces `MDRewriter.app` and `macdeployqt` embeds Qt and creates the `.dmg`. The app is unsigned: Gatekeeper asks for right-click "Open", and signing and notarizing need an Apple developer account (`macdeployqt -codesign=...`). The port appears as `/dev/cu.usbmodem*`. The project has no `.icns` icon yet.

```bash
brew install qt                              # Qt 6 with Qt Serial Port
mkdir build && cd build
"$(brew --prefix qt)/bin/qmake" ../Software/MDRewriter.pro CONFIG+=release
make -j"$(sysctl -n hw.ncpu)"
"$(brew --prefix qt)/bin/macdeployqt" MDRewriter.app -dmg      # MDRewriter.dmg
```

## How to use

1. **Plug the MD Rewriter** into your computer over USB and **insert the cartridge** into the device.
2. Start **MD Rewriter**. The port list refreshes by itself, and the device port (USB `0483:5740`, "STM32 Virtual ComPort") is selected automatically. If it is missing, click **Refresh**.
3. Click **Connect**. The log shows "Device identified." and the action buttons become available. Buttons are greyed out when not connected and while an operation is running.
4. Click **Detect cartridge** to identify the flash chip (shows the chip ID and the model when known). Firmware 1.0 only recognises the MX29LV640EB and prints `NO FIND NGP CARD` for any other chip, but that blocks nothing: the program names other chips from the ID (for example the ST M29W640D) and explains the result.

### Back up a game (read the ROM)

1. Under **Game (ROM)**, choose the **Cartridge size** (512 KiB, 1 MiB, 2 MiB or 4 MiB).
2. Click **Read ROM…** and choose where to save the file (`.bin`).
3. Watch the progress bar. The file is only created when the read finishes completely and without errors. If you cancel or something fails, **no partial file is left behind**.

### Write a game to a flash cartridge

1. **Erase the cartridge first** if it is not blank (see below).
2. Click **Write ROM…** and choose the file (`.bin`, `.md`, `.gen` or `.smd`). Any ROM of even size between 512 bytes and 4 MiB is accepted (256 KiB, 768 KiB, 1.5 MiB, ...): it is written as it is and the rest of the chip stays blank. `.smd` files are converted to a plain ROM. The program warns, without blocking, if the header looks wrong (no `SEGA`, bad checksum, truncated file). With **Verify after writing** ticked (the default) it reads the cartridge back and compares it with the file when the write ends.
3. Confirm the warning. **Cancel** stops the write but leaves the cartridge holding a partial image.

### Erase the cartridge

- **Erase by size…**: erases only the flash sectors of the size chosen in "Cartridge size". Faster.
- **Erase all…**: erases the whole flash chip and the save memory. It can take several minutes and **cannot be cancelled**. Do not unplug the device while it runs.

Both ask for confirmation because they **destroy the data on the cartridge**.

### Save data (SRAM)

- Choose the **Save size** (32 KiB or 8 KiB) and click **Read save…** to copy the cartridge's saves into a file (`.srm`).
- **Write save…** replaces the saves on the cartridge with the contents of an 8 KiB or 32 KiB file (asks for confirmation).

### Compare files

**Compare files…** asks for two files and lists the first 20 differences (byte offsets). Use it to check that a dump is identical to the original ROM. No device needed.

### Troubleshooting

| Symptom | What to do |
|---------|------------|
| "permission denied" when connecting (Linux) | `dialout` group or the udev rule (installation section). |
| The port does not appear | Try another cable (a data cable, not charge-only), replug, and click **Refresh**. |
| "the device did not answer" | Wrong port, or the firmware is not flashed on the device. |
| Read reports "incomplete or out of sync" | Try another cable/USB port and read again; the file is not saved in that case. |
| A port the system does not list | Set `MD_REWRITER_EXTRA_PORT=/path/to/port` before starting the program. |

## Development and tests

```bash
cd Software/tests
qmake6 tests.pro && make -j"$(nproc)" && make check     # unit, integration and end-to-end tests
cd ../.. && tools/check-firmware.sh                      # static firmware check (arm-none-eabi-gcc, cppcheck)
```

- `tst_protocol`: unit tests (commands, size tables, stream parser, file comparer).
- `tst_session`: integration tests (device session against a simulated firmware, with fragmented replies, faults and cancellation).
- `tst_help`: built-in guide (structure, languages, links) and a check that the messages and keys quoted in the guide exist in the program.
- `tst_e2e`: end-to-end tests (real window + real serial port on a pseudo-terminal + simulated firmware; Linux only). Set `QT_QPA_PLATFORM=offscreen` to run without a display.

The firmware changes could not be tested on real hardware: see `docs/SPEC.md` and `docs/PLAN.md`.

## License

[MIT](LICENSE) license. Third-party code in `legacy_firmware/Drivers` and `legacy_firmware/Middlewares` (STMicroelectronics, ARM) keeps its original licenses.
