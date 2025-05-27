# ImageProcNet - Procesare Distribuita de Imagini

Acest proiect este o aplicatie client-server pentru procesarea distribuita a imaginilor, realizata in limbajul C. Comunicarea se face prin socket TCP/IP si UNIX domain socket pentru partea de administrare. Procesarea imaginilor este realizata cu ajutorul comenzii `convert` din ImageMagick, iar serverul suporta trimiterea imaginilor de la mai multi clienti concurenti, procesarea lor si returnarea rezultatului.

## Descriere generala

Proiectul demonstreaza gestionarea comunicatiei intre procese prin socket TCP si socket UNIX, utilizarea firelor de executie (pthread) si sincronizarea acestora prin mutex si conditii. Este implementat si un modul de administrare care permite controlul conexiunilor si monitorizarea procesarilor.

## Structura generala

- `server.c` – serverul principal care accepta conexiuni TCP si proceseaza imaginile
- `client.c` – clientul TCP care trimite imaginea si primeste rezultatul
- `admin_client.c` – client de administrare ce comunica prin UNIX socket cu serverul
- Director `user_temp/` – imaginile primite temporar

## Functionalitati implementate

### Functii server
- Accepta conexiuni multiple prin fire (`pthread`)
- Primeste o imagine JPEG de la client
- Coada de taskuri protejata prin mutex si conditie
- Procesare imagine:
  - Resize (50%)
  - Grayscale (tonuri de gri)
  - Blur (estompare)
- Returneaza imaginea procesata catre client
- Salveaza informatii despre procesare intr-un istoric local (nume fisier, marime initiala, marime finala, durata)

### Functii administrative
- `LIST` – Afiseaza IP-urile si porturile clientilor conectati
- `HISTORY` – Afiseaza istoricul procesarilor efectuate
- `STATS` – Afiseaza durata medie de procesare
- `KICK <ip>` – Deconecteaza fortat un client activ
- `LIMIT <ip>` – Blocheaza conexiunile noi de la un IP
- `UNBLOCK <ip>` – Deblocheaza un IP
- `QUIT` – Inchide socketul UNIX de administrare (doar pentru testare)

### Functii client
- Trimite o imagine locala catre server
- Alege dintr-un meniu tipul de procesare dorit:
  - 1 = Resize
  - 2 = Grayscale
  - 3 = Blur
- Primeste imaginea procesata inapoi
- Afiseaza mesaje de eroare daca IP-ul este blocat

## Cerinte

- Linux sau sistem compatibil POSIX
- `gcc` cu suport pentru `pthread`
- ImageMagick instalat (`convert`)
- Fisiere `.jpg` pentru testare

## In lucru

- Interfata grafica pentru client 
  Implementarea unei interfete grafice (de tip GTK sau Qt) pentru trimiterea imaginilor si afisarea rezultatului procesat.

## Functionalitati optionale propuse (neimplementate)

- Persistenta istoricului in fisier
- Extinderea suportului pentru alte tipuri de imagini (PNG, BMP)
- Optiuni suplimentare de procesare (crop, sharpen, watermark)
- Interfata grafica completa pentru client
- Logare detaliata a activitatilor

## Instructiuni de rulare

1. Compileaza serverul si porneste-l:

