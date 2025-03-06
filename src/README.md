Maholea Elena-Alexia ---- 333 CD

--------------------------------------------

Tema 2 APD - Protocolul BitTorrent
-----------------------

Pentru a implementa tema, am considerat astfel:


<h2 style="text-align: center;">TRACKER</h2>

- mai întâi tracker-ul primește de la fiecare peer în parte
fișierele pe care le deține fiecare și toate segmentele acestora

- salvez fișierele într-o structură, având hash-urile și numărul acestora, precum și persoanele care dețin părți din ele

- note: dacă deja avem hash-urile unui fișier salvate, nu le adăugăm iar,
ci doar dăm update la lista de seeds

- după ce tracker-ul termină de procesat toate fișierele, trimite fiecărui
peer în parte un ACK pentru a putea da drumul firelor de download și upload

- apoi, tracker-ul e pregătit să primească diferite mesaje:
    - dacă cineva face un download request, tracker-ul îi va trimite informațiile necesare legate de file-ul respectiv, precum ownerii și numărul de chunk-uri, dar va adăuga și peer-ul care a trimis request-ul în swarm-ul fișierului
    - dacă cineva face un update request, tracker-ul va trimite doar
    lista de owneri actualizată
    - dacă cineva trimite mesaj că a terminat de downloadat un file, tracker-ul caută fișierul după nume și îi actualizează swarm-ul: șterge taskul care a trimis mesajul din lista de peers și îl adaugă la seeds
    - iar ultimul tip de mesaj pe care tracker-ul îl procesează este DONE, care ne ajută să știm când putem închide și firele de upload

<h2 style="text-align: center;">PEER</h2>

- pentru partea de citire a datelor, am trimis fișierele pe care
un peer le deținea tracker-ului pentru a construi baza de date, dar
le-am salvat și într-un vector de structuri files_owned, care mă
va ajuta pentru partea de upload, când am nevoie să verific dacă
un peer are chunk-ul pentru a-l putea trimite

- pentru fișierele pe care un peer vrea sa le descarce, am creat o structură file_request în care o să salvez informațiile de care am nevoie și pe care o să i le ceară acesta tracker-ului

# DOWNLOAD THREAD

- pentru download thread, se ia fiecare fișier dorit în parte și se realizează un info file request, în care i se cere tracker-ului swarm-ul fișierului, precum și numărul de chunks din care este alcătuit

- după ce primim acestea, adăugăm fișierul in vectorul de files owned,
pentru a putea primi cereri de la alți peers, dar inițializăm chunk-urile cu
NULL, pentru a putea confirma dacă avem chunk-ul sau încă nu

- apoi începem să descărcăm pe rând fiecare chunk de care avem nevoie

- aici am folosit tehnica round robin, cerând pe rând de la fiecare peer câte un chunk pentru a nu îi aglomera prea tare. De asemenea, m-am gândit că adăugând întâi seeds în lista de owners, apoi peers, am crescut șansa de a primi un ACK înapoi (pentru că le dă timp celor care nu au tot fișierul descărcat să mai descarce din chunk-uri)

- după ce primim chunk-ul, îl adăugăm în lista noastră pentru a-l putea da mai departe și altora și pentru a crea fișierul de output de la final

- pentru fiecare segment primit, cresc variabila download, pentru a ști când e nevoie să facem o cerere de actualizare de la tracker

- după ce terminăm de descărcat toate fișierele, trimitem un mesaj de DONE către tracker pentru a putea ține evidența și a închide upload threads când termină toți


# UPLOAD THREAD

- când cineva face un request cu tag-ul de UPLOAD, avem 2 cazuri:
    - fie este un request de DOWNLOAD, iar în acest caz, cerem numele file-ului și index-ul hash-ului care este dorit și verificăm mai întâi
    dacă deținem fișierul respectiv, iar apoi dacă am apucat să descărcăm chunk-ul. Dacă nu deținem una dintre acestea, trimitem un NACK, iar în caz contrar trimitem ACK și hash-ul care se dorea
    - dacă este un mesaj de DONE, atunci înseamnă că toți clienții au terminat de descărcat, deci putem închide upload thread-ul