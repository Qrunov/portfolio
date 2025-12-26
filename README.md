Справка<br>
portfolio help<br>
Справка по командам<br>
portfolio <command> help<br>
Справка по командам c опциями выбранных плагинов<br>
portfolio <command> help --with csv --with sqlite_db<br><br><br><br><br><br><br>



Основной сценарий работы<br>
Посмотреть список плагинов<br>
portfolio plugin list<br>
Создать портфель<br>
portfolio portfolio create -n test1<br>
Посмотреть список инструментов<br>
portfolio instrument list -db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db<br>
Посмотреть детальную информацию об инструменте<br>
portfolio instrument show -t SBER --db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db<br>
Добавить инструмент в портфель<br>
portfolio portfolio add-instrument --instrument-id SBER -p test1<br>
Посмотреть детальную информацию о портфеле<br>
portfolio portfolio show -p test1<br>
Изменить параметр стратегии в заданном портфеле<br>
portfolio set-param -p MyPortfolio -P tax:true<br>
Выполнить бактест стратегии<br>
portfolio strategy execute -s buyhold_strategy -p MyPortfolio <br>
--from 2015-12-15 --to 2025-10-15 --db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db
