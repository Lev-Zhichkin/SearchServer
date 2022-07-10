# SearchServer
Поисковик документов с возможностью указания минус-слов, которые не учитываются в выдаче.
Пусть, например, в системе есть такие документы:  
1. `белый кот и модный ошейник`
2. `пушистый кот пушистый хвост`
3. `ухоженный пёс выразительные глаза`  

По запросу `кот` найдутся документы 1 и 2.  
По запросу `кот -ошейник -пушистый` не найдётся ничего, потому что `-ошейник` исключило документ 1, а `-пушистый` — документ 2. Порядок слов не имеет значения.

- Если в запросе нет плюс-слов, ничего не найдется.
- Если одно и то же слово будет в запросе и с минусом, оно считается минус-словом.  
- Ранжирование результата происходит по **TF-IDF**.
- Функция нахождения документов по запросу имеет последовательную и параллельные версии.

---
### Пример использования кода:
```C++
    SearchServer search_server("and with"s);

    for (
        int id = 0;
        const string & text : {
        "white cat and yellow hat"s, "curly cat curly tail"s, "nasty dog with big eyes"s, "nasty pigeon john"s,
        }
    ) 
    {
        search_server.AddDocument(++id, text, DocumentStatus::ACTUAL, { 1, 2 });
    }

    cout << "ACTUAL by default:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments("curly nasty cat"s)) {
        PrintDocument(document);
    }
   cout << "BANNED:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(execution::seq, "curly nasty cat"s, DocumentStatus::BANNED)) {
        PrintDocument(document);
    }

    cout << "Even ids:"s << endl;
    // параллельная версия
    for (const Document& document : search_server.FindTopDocuments(execution::par, "curly nasty cat"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        PrintDocument(document);
    }
 ```
Результат:
```
ACTUAL by default:
{ document_id = 2, relevance = 0.866434, rating = 1 }
{ document_id = 4, relevance = 0.231049, rating = 1 }
{ document_id = 1, relevance = 0.173287, rating = 1 }
{ document_id = 3, relevance = 0.173287, rating = 1 }
BANNED:
Even ids:
{ document_id = 2, relevance = 0.866434, rating = 1 }
{ document_id = 4, relevance = 0.231049, rating = 1 }
```
---
