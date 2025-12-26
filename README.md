Справка
portfolio help
Справка по командам
portfolio <command> help
Справка по командам c опциями выбранных плагинов
portfolio <command> help --with csv --with sqlite_db



Основной сценарий работы
Посмотреть список плагинов
portfolio plugin list
Создать портфель
portfolio portfolio create -n test1
Посмотреть список инструментов
portfolio instrument list -db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db
Посмотреть детальную информацию об инструменте
portfolio instrument show -t SBER --db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db
Добавить инструмент в портфель
portfolio portfolio add-instrument --instrument-id SBER -p test1
Посмотреть детальную информацию о портфеле
portfolio portfolio show -p test1
Изменить параметр стратегии в заданном портфеле
portfolio set-param -p MyPortfolio -P tax:true
Выполнить бактест стратегии
portfolio strategy execute -s buyhold_strategy -p MyPortfolio \
--from 2015-12-15 --to 2025-10-15 --db sqlite_db --sqlite-path=/usr/share/portfolio_sample.db
