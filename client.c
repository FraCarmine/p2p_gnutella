#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAXLEN 255
#define MAXOUTGOING 3
#define LOCALHOST "127.0.0.1"
#define MAXINCOMING 5
#define TYPE_PING 1
#define TYPE_QUERY 2
#define TYPE_PONG 3
#define TYPE_QUERYHIT 4
#define MAXMESSAGE 100
#define MAX_FILENAME 128 // lunghezza massima del nome del file
#define MAX_RESULTS 3 // numero massimo di risultati per query
#define NMAXFILE 4 // numero massimo di file per peer

//#define  PORT 3333

typedef struct RoutingEntry {
    int id;
    int sockfd; // -1 se sono io il mittente originale 0 se il socket è libero
} RoutingEntry;

typedef struct Peer {
    int sd;
    struct sockaddr_in addr;
    int active; // 0 = slot libero, 1 = usato
} Peer;

//----------------------------Struttura messaggi----------------------------------------------------------------------------------------------

typedef struct MessageHeader {
    int type; // 1 per ping, 2 per query, 3 pong, 4 queryhit
    int id; // ID univoco numerico
    int ttl; // Time To Live
    int payload_length; // lunghezza del payload
} MessageHeader;

typedef struct PongPayload {
    int port; // porta del peer
    char ip[MAXLEN]; // indirizzo IP del peer
} PongPayload;

typedef struct QueryPayload{
    int minimum_speed; // velocità minima richiesta
    char query[MAXLEN]; // query da inviare
} QueryPayload;


typedef struct file_result {
    int index;
    char name[MAX_FILENAME];  // es. 128 byte
} file_result;

typedef struct query_hit_payload{
    int n_hits;
    int port;
    char ip[MAXLEN]; // indirizzo IP del peer che ha risposto
    int speed;
    file_result results[MAX_RESULTS];  // es. massimo 10 file trovati
} query_hit_payload;


//---------------------------------------------------------------------------------------------------------------------------------------------------
//

void die(char *);
void stampaPeer(Peer* incoming_peers, Peer* outgoing_peers);
void chiudiConnessioni(Peer* incoming_peers, Peer* outgoing_peers);
int riceviMessaggio(int sd, MessageHeader* header);
int connectToPeer(char *ip, Peer* outgoing_peers, int* maxfd, fd_set* readFDSET, int listenPort);
int ricercaDuplicato(RoutingEntry* routingTable, int id);
int handlePong(int sd, MessageHeader* header, RoutingEntry* routingTable);
int rispondiPing (int sd, RoutingEntry* routingTable, MessageHeader* header, Peer* outgoing_peers, Peer* incoming_peers, struct sockaddr_in peer_addr);
int ping(Peer* outgoing_peers, Peer* incoming_peers, RoutingEntry* routingTable);
int query(Peer* outgoing_peers, Peer* incoming_peers, RoutingEntry* routingTable);
int handleQuery(int sd, MessageHeader* header, Peer* outgoing_peers, Peer* incoming_peers, RoutingEntry* routingTable, int mySpeed, struct sockaddr_in bind_ip_port, int listenPort, char fs[NMAXFILE][MAXLEN]);
int handleQueryHit(int sd, MessageHeader* header, RoutingEntry* routingTable);
int popolaFileSystem(char fs[NMAXFILE][MAXLEN], int nmaxfile);
int disconnectPeer(Peer* peer,fd_set* readFDSET);

int main() {
	int listenSocket;//socket dal quale ascolto chi si vuole connettere con me
    int newsd; //socket per la nuova connessione
	Peer incoming_peers[MAXINCOMING]; //socket per i peer che si sono connessi con me
	Peer outgoing_peers[MAXOUTGOING]; //socket per i peer che mi sono connesso
    int listenPort;
    int wellKnownPort;
    int maxfd; //massimo descrittore di file
    int scelta;
    int flag = 1;
    int n; //variabile per il numero di descrittori pronti
    int p;
    int mySpeed = 100; // variabile per la velocità 100kbps
    char fs[NMAXFILE][MAXLEN]; // stringa per il nome del file


    MessageHeader header; //header del messaggio
    RoutingEntry routingTable[MAXMESSAGE]; //tabella di routing per i messaggi
    srand(time(NULL)); // inizializza il generatore di numeri casuali UNA SOLA VOLTA

    //inizializzo la tabella di routing
    for(int i = 0; i < MAXMESSAGE; i++) {
        routingTable[i].sockfd = 0; // inizializzo il socket a 0 (libero)
        routingTable[i].id = 0; // inizializzo l'ID a 0
    }

 	//parto con la listen
	struct sockaddr_in bind_ip_port;
	int bind_ip_port_length = sizeof(bind_ip_port);
    
    struct timeval time;
    time.tv_sec =30;
    time.tv_usec=10;
    
    fd_set temp, readFDSET;
    
    listenSocket = socket(AF_INET, SOCK_STREAM, 0 ); //SOCKET DI ASCOLTO
    if(listenSocket <0 ) die("\nsocket() error");
    printf("\n socket ascolto ok \n");
   
    if(setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))<0){
        die("error soREuseADDR \n");
    }
	if(setsockopt(listenSocket, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int))<0){
        die("error soREuseADDR \n");
    }
    printf("inserire porta in ascolto\n");
    scanf("%d", &listenPort);
    while(getchar() !='\n');    //pulisco il buffer
    while (listenPort < 1024 || listenPort > 65535) {
        printf("Porta non valida. Deve essere compresa tra 1024 e 65535.\n");
        printf("inserire porta in ascolto\n");
        scanf("%d", &listenPort);
        while(getchar() !='\n'); // pulisco il buffer
    }

    //binding
    bind_ip_port.sin_family = AF_INET; //IPV4
    bind_ip_port.sin_addr.s_addr = inet_addr(LOCALHOST);
    bind_ip_port.sin_port = htons(listenPort);

    if(bind(listenSocket, (struct sockaddr *)&bind_ip_port, bind_ip_port_length) < 0) {
        die("\nbind() error");
    }
    printf("\nbind ok\n");

    //inizializzo il set dei descrittori per la select
    FD_ZERO(&readFDSET);
    FD_SET(STDIN_FILENO, &readFDSET); //aggiungo lo standard input al set
    maxfd = STDIN_FILENO; //inizializzo il massimo descrittore di file a 0 (standard input)

    //---------------------------------------------------------inizializzo------------------------------------------------------------
    //inizializzo i peer in arrivo e in uscita
    for(int i = 0; i < MAXINCOMING; i++) {
        incoming_peers[i].sd = -1; // inizializzo i socket dei peer in arrivo a -1 (libero)
        incoming_peers[i].active = 0; // inizializzo lo stato come non attivo
    }
    for(int i = 0; i < MAXOUTGOING; i++) {
        outgoing_peers[i].sd = -1; // inizializzo i socket dei peer in uscita a -1 (libero)
        outgoing_peers[i].active = 0; // inizializzo lo stato come non attivo
    }   
    //inizializzo il socket di ascolto
    if(listen(listenSocket, MAXINCOMING) < 0) {
        die("\nlisten() error");
    }
    printf("\nlisten ok\n");
    printf("\nServer in ascolto su %s:%d\n", LOCALHOST, listenPort);


    FD_SET(listenSocket, &readFDSET); //aggiungo il socket di ascolto al set
    if(listenSocket > maxfd) {
        maxfd = listenSocket; // aggiorno il massimo descrittore di file
    }
    popolaFileSystem(fs, NMAXFILE); //popolo il file system con i file di esempio
    //---------------------------------------------------------fine inizializzazione------------------------------------------------------------

    //
    // Ciclo principale del server
    //
    printf("inserire well konwn peer\n");
    if(connectToPeer(LOCALHOST,  outgoing_peers, &maxfd, &readFDSET, listenPort) < 0) {
        printf("Errore nella connessione al peer well known");
    }
    
    //menu utente testuale
    printf("\n\nMenu:\n 1. Esegui un ping \n  2. esegui una query \n 3. aggiungi peer \n 4. listPeer \n 5. esci\n 6. disconnetti peer\n");



    // Ciclo principale per gestire le connessioni e le comunicazioni
    while (flag){
        memcpy(&temp, &readFDSET, sizeof(temp)); //copio il set di lettura per la select dentro temp che verra modificato da select
        n= select(maxfd+1, &temp, NULL, NULL, &time); //attendo eventi sui socket
        
        if(n>0){    //almeno un socket è pronto
            if(FD_ISSET(STDIN_FILENO, &temp)){  //è Pronto lo stdin
                scanf("%d", &scelta);
                while(getchar() !='\n'); //pulisco il buffer

                switch (scelta) {
                    case 1: //fa il ping
                        printf("Eseguo ping...\n");
                        p=ping(outgoing_peers, incoming_peers, routingTable);
                        if(p< 0) {
                            printf("Errore nell'invio del ping.\n");
                        } else {
                            if (p==0){
                                printf("Nessun peer attivo per il ping.\n");
                            } else {
                                printf("Ping inviato con successo a %d peer.\n", p);
                            }
                        }
                        break;
                    
                    case 2: //esegue una query
                        printf("Eseguo query...\n");
                        p=query(outgoing_peers, incoming_peers, routingTable);
                        if(p < 0) {
                            printf("Errore nell'invio della query.\n");
                        } else {
                            if(p == 0) {
                                printf("Nessun peer attivo per la query.\n");
                            } else {
                                printf("Query inviata con successo a %d peer.\n", p);
                            }
                        }
                        break;
                    
                    case 3:     //aggiungi peer
                        printf("Aggiungi peer...\n");
                        if(connectToPeer(LOCALHOST, outgoing_peers, &maxfd, &readFDSET, listenPort)<0) {
                            printf("Errore nella connessione al peer.\n");
                        } else {
                            printf("Connessione al peer aggiunto con successo.\n");
                        }
                        break;
                    
                    case 4: //list peer
                        printf("Lista dei peer:\n");
                        stampaPeer(incoming_peers, outgoing_peers);
                        break;
                    
                    case 5: //esci
                        printf("Uscita dal client...\n");
                        flag = 0; //esce dal ciclo principale
                        chiudiConnessioni(incoming_peers, outgoing_peers);
                        close(listenSocket); //chiude il socket di ascolto
                        break;
                    case 6: //disconnetti peer
                        printf("Disconnetti peer...\n");
                        int disconnectPort;
                        //printf("inserire indirizzo IP del peer da disconnettere: ");
                        //scanf("%s", ip); questo nel caso in cui avessi un client che non opera su localhost
                        printf("inserisci la porta del peer da disconnettere: ");
                        scanf("%d", &disconnectPort);
                        while(getchar() !='\n'); // pulisco il buffer
                        for(int i = 0; i < MAXOUTGOING; i++) {
                            if(outgoing_peers[i].active && ntohs(outgoing_peers[i].addr.sin_port) == disconnectPort && inet_addr(LOCALHOST) == incoming_peers[i].addr.sin_addr.s_addr) {
                                if(disconnectPeer(&outgoing_peers[i], &readFDSET) < 0) {
                                    printf("Errore nella disconnessione del peer.\n");
                                } else {
                                    printf("Peer disconnesso con successo.\n\n\n");
                                }
                                break;
                            }
                        }
                        for(int i = 0; i < MAXINCOMING; i++) {
                            if(incoming_peers[i].active && ntohs(incoming_peers[i].addr.sin_port) == disconnectPort && inet_addr(LOCALHOST) == incoming_peers[i].addr.sin_addr.s_addr) {//espandibile con il controllo su indirizzo IP
                                if(disconnectPeer(&incoming_peers[i], &readFDSET) < 0) {
                                    printf("Errore nella disconnessione del peer.\n");
                                } else {
                                    printf("Peer disconnesso con successo.\n\n\n");
                                }
                                break;
                            }
                        }                    
                        break;
                }  
                
                //menu utente testuale
                printf("\n\nMenu:\n 1. Esegui un ping \n 2. esegui una query \n 3. aggiungi peer \n 4. listPeer \n 5. esci\n 6. disconnetti peer\n");
            }
            if(!flag){
                break; //esce dal ciclo principale se l'utente ha scelto di uscire
            }

            //----------------------------------------------------LISTEN SOCKET-------------------------------------------------
            //controllo se ci sono nuovi peer in arrivo da accettare
            if(FD_ISSET(listenSocket, &temp)) {
                for(int i = 0; i < MAXINCOMING; i++) {
                    if(incoming_peers[i].active == 0) { //trova uno slot libero
                        newsd = accept(listenSocket, (struct sockaddr *)&incoming_peers[i].addr, &bind_ip_port_length);
                        if(newsd < 0) {
                            die("\naccept() error");
                        }
                        incoming_peers[i].sd = newsd;
                        incoming_peers[i].active = 1; // segna il peer come attivo
                        FD_SET(newsd, &readFDSET); // aggiungo il socket del peer al set di lettura
                        if(newsd > maxfd) {
                            maxfd = newsd; // aggiorno il massimo descrittore di file
                        }
                        printf("Nuova connessione da %s:%d\n", inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
                        break; // esce dal ciclo dopo aver accettato una connessione
                    }
                    if(i == MAXINCOMING-1) {//tutti gli slot sono occupati
                        printf("Massimo numero di peer in arrivo raggiunto.\n");
                    }
                }
                
            }
            //-------------------------------------------------RICEZIONE MESSAGGI-------------------------------------------------
            //controllo se ci sono peer in uscita pronti
            for(int i = 0; i < MAXOUTGOING; i++) {
                if(outgoing_peers[i].active && FD_ISSET(outgoing_peers[i].sd, &temp)) {
                    // ricevo il messaggio dal peer in uscita
                    p= riceviMessaggio(outgoing_peers[i].sd, &header);
                    if(p == 0) {
                        perror("Errore nella ricezione del messaggio dal peer in uscita");
                        close(outgoing_peers[i].sd); // chiudo il socket del peer
                        outgoing_peers[i].active = 0; // segna il peer come non attivo
                        FD_CLR(outgoing_peers[i].sd, &readFDSET); // rimuovo il socket dal set di lettura
                        continue;
                    } 
                    if(p < 0) {
                        printf("Errore nella ricezione del messaggio dal peer in uscita.\n");
                        continue; // continua il ciclo se c'è un errore
                    }
                    else{
                        // Gestisco il messaggio ricevuto
                        switch(ntohs(header.type)) {
                            case TYPE_PING:
                                printf("Ricevuto ping da %s:%d\n", inet_ntoa(outgoing_peers[i].addr.sin_addr), ntohs(outgoing_peers[i].addr.sin_port));
                                // Rispondo con un pong
                                if(rispondiPing(outgoing_peers[i].sd, routingTable, &header, outgoing_peers, incoming_peers, bind_ip_port) < 0) {
                                    perror("Errore nella risposta al ping");
                                } else {
                                    printf("Pong inviato con successo.\n");

                                }
                                break;
                            case TYPE_PONG:
                                printf("Ricevuto pong da %s:%d\n", inet_ntoa(outgoing_peers[i].addr.sin_addr), ntohs(outgoing_peers[i].addr.sin_port));
                                handlePong(outgoing_peers[i].sd, &header, routingTable);
                                break;

                            case TYPE_QUERY:
                                printf("Ricevuto query da %s:%d\n", inet_ntoa(outgoing_peers[i].addr.sin_addr), ntohs(outgoing_peers[i].addr.sin_port));
                                //inoltro la query a tutti i peer
                                handleQuery(outgoing_peers[i].sd, &header, outgoing_peers, incoming_peers, routingTable, mySpeed, bind_ip_port, listenPort, fs);
                                break;
                            
                            case TYPE_QUERYHIT:
                                printf("Ricevuto queryhit da %s:%d\n", inet_ntoa(outgoing_peers[i].addr.sin_addr), ntohs(outgoing_peers[i].addr.sin_port));
                                // Gestisco il queryhit
                                if(handleQueryHit(outgoing_peers[i].sd, &header, routingTable)<0){
                                    perror("Errore nella gestione del queryhit");
                                } else {
                                    printf("Queryhit gestito con successo.\n");
                                }
                                break;
                        }
                    }
                }
            }
            
            //--------------------------------------DA INCOMING-------------------------------------------------
            for(int i = 0; i < MAXINCOMING; i++) {
                if(incoming_peers[i].active && FD_ISSET(incoming_peers[i].sd, &temp)) {
                    p= riceviMessaggio(incoming_peers[i].sd, &header);
                    if(p == 0) {
                        perror("Errore nella ricezione del messaggio dal peer in arrivo");
                        close(incoming_peers[i].sd); // chiudo il socket del peer
                        incoming_peers[i].active = 0; // segna il peer come non attivo
                        FD_CLR(incoming_peers[i].sd, &readFDSET); // rimuovo il socket dal set di lettura
                        continue;
                    }
                    if(p < 0) {
                        printf("Errore nella ricezione del messaggio dal peer in arrivo.\n");
                        continue; // continua il ciclo se c'è un errore
                    }
                    else{
                        // Gestisco il messaggio ricevuto
                        switch(ntohs(header.type)) {
                            case TYPE_PING:
                                printf("Ricevuto ping da %s:%d\n", inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
                                // Rispondo con un pong
                                if(rispondiPing(incoming_peers[i].sd, routingTable, &header, outgoing_peers, incoming_peers, bind_ip_port) < 0) {
                                    perror("Errore nella risposta al ping");
                                } else {
                                    printf("Pong inviato con successo.\n");
                                }
                                break;
                            case TYPE_PONG:
                                printf("Ricevuto pong da %s:%d\n", inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
                                handlePong(incoming_peers[i].sd, &header, routingTable);
                                break;

                            case TYPE_QUERY:
                                printf("Ricevuto query da %s:%d\n", inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
                                //inoltro la query a tutti i peer
                                handleQuery(incoming_peers[i].sd, &header, outgoing_peers, incoming_peers, routingTable, mySpeed, bind_ip_port, listenPort, fs);
                                break;
                            
                            case TYPE_QUERYHIT:
                                printf("Ricevuto queryhit da %s:%d\n", inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
                                // Gestisco il queryhit
                                if(handleQueryHit(incoming_peers[i].sd, &header, routingTable)<0){
                                    perror("Errore nella gestione del queryhit");
                                } else {
                                    printf("Queryhit gestito con successo.\n");
                                }
                                break;
                        }
                    }
                }
            }
            
        }
    }
}


//-------------------------------------------------------Utilities------------------------------------------------------------
 
int disconnectPeer(Peer* peer,fd_set* readFDSET) {
    // Funzione per disconnettere un peer
    if(peer->active) {
        close(peer->sd);
        peer->active = 0; // segna il peer come non attivo
        printf("Connessione con peer %s:%d chiusa.\n", inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port));
        FD_CLR(peer->sd, readFDSET); // rimuovo il socket dal set di lettura
        peer->sd = -1; // resetto il socket a -1 (libero
        return 0; // ritorna 0 se tutto va bene
    }
    return -1; // ritorna -1 se il peer non è attivo
}

int popolaFileSystem(char fs[NMAXFILE][MAXLEN], int nmaxfile) {
    // Funzione per popolare il file system con i file di esempio
    int len;
    printf("Popolamento del file system con %d file di esempio...\n", nmaxfile);
    for(int i = 0; i < nmaxfile; i++) {
        printf("\ninserire nome del file:  ");
        if (fgets(fs[i], MAXLEN, stdin) != NULL) {
            int len = strlen(fs[i]);
            if (len > 0 && fs[i][len - 1] == '\n') {
                fs[i][len - 1] = '\0'; // metto il terminatore di stringa al posto del a capo
            }
            else{
                fs[i][0] = '\0';
            }
        } 
    }
    return 0; // ritorna 0 se tutto va bene
}



int riceviMessaggio(int sd, MessageHeader* header) {
    // Funzione per ricevere un messaggio da un peer
    int s;
    s=recv(sd, header, sizeof(MessageHeader), 0);
    if(s == 0) {
        printf("connessione chiusa dal peer.\n");
        return 0; // connessione chiusa dal peer
    }
    if(s < 0) {
        perror("Errore nella ricezione del messaggio");
        return -1; // errore nella ricezione del messaggio
    }
    return 1; // messaggio ricevuto con successo
    }


void chiudiConnessioni(Peer* incoming_peers, Peer* outgoing_peers){
    for(int i = 0; i < MAXINCOMING; i++) {
        if(incoming_peers[i].active) {
            close(incoming_peers[i].sd);
            incoming_peers[i].active = 0; // segna il peer come non attivo
            printf("Connessione con peer  %s in arrivo chiusa.\n", inet_ntoa(incoming_peers[i].addr.sin_addr));
        }
    }

    for(int i = 0; i < MAXOUTGOING; i++) {
        if(outgoing_peers[i].active) {
            close(outgoing_peers[i].sd);
            outgoing_peers[i].active = 0; // segna il peer come non attivo
            printf("Connessione con peer %s in uscita chiusa.\n", inet_ntoa(outgoing_peers[i].addr.sin_addr));
        }
    }
    printf("Tutte le connessioni chiuse.\n");
}


void stampaPeer(Peer* incoming_peers, Peer* outgoing_peers) {
    printf("Peer in arrivo:\n");
    for(int i = 0; i < MAXINCOMING; i++) {
        if(incoming_peers[i].active) {
            printf("Peer %d: %s:%d\n", i, inet_ntoa(incoming_peers[i].addr.sin_addr), ntohs(incoming_peers[i].addr.sin_port));
        }
    }

    printf("Peer in uscita:\n");
    for(int i = 0; i < MAXOUTGOING; i++) {
        if(outgoing_peers[i].active) {
            printf("Peer %d: %s:%d\n", i, inet_ntoa(outgoing_peers[i].addr.sin_addr), ntohs(outgoing_peers[i].addr.sin_port));
        }
    }
}



int connectToPeer(char *ip, Peer* outgoing_peers, int* maxfd, fd_set* readFDSET, int listenPort) {
    int port, i;
    struct sockaddr_in peer_addr;
    // Chiede all'utente di inserire la porta del peer
    printf("inserire porta del peer\n");
    scanf("%d", &port);
    while(getchar() !='\n'); // pulisco il buffer
    while(port < 1024 || port > 65535 || port == listenPort) { // verifica che la porta sia valida e non sia la stessa della porta di ascolto
        printf("Porta non valida. Deve essere compresa tra 1024 e 65535.\n");
        printf("inserire porta del peer\n");
        scanf("%d", &port);
        while(getchar() !='\n'); // pulisco il buffer
    }

    // Creazione del socket per il peer
    for(i = 0; i < MAXOUTGOING; i++) {
        if (outgoing_peers[i].active == 0) { // trova un slot libero
            break; // esce dal ciclo se trova uno slot libero
        }
    }
    if (i == MAXOUTGOING ) {
            printf("Massimo numero di peer in uscita raggiunto.\n");
            return -1; // nessuno slot libero
        }

    outgoing_peers[i].sd = socket(AF_INET, SOCK_STREAM, 0);
    if (outgoing_peers[i].sd < 0) {
        die("socket() error");
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    peer_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(outgoing_peers[i].sd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        close(outgoing_peers[i].sd);
        return -1; // errore di connessione
    }
    outgoing_peers[i].addr = peer_addr;
    outgoing_peers[i].active = 1; // segna il peer come attivo
    printf("Connessione al peer %s:%d stabilita con successo.\n", ip, port);
    
    FD_SET(outgoing_peers[i].sd, readFDSET); // aggiungo il socket del peer al set di lettura
    if(outgoing_peers[i].sd > *maxfd) {  
        (*maxfd) = outgoing_peers[i].sd; // aggiorno il massimo descrittore di file
    }
    return 1;
}


int ricercaDuplicato(RoutingEntry* routingTable, int id) {
    // Funzione per cercare un duplicato nella tabella di routing
    for(int i = 0; i < MAXMESSAGE; i++) {
        if(routingTable[i].id == id) {
            return i; // trovato duplicato
        }
    }  
    return -1; // nessun duplicato trovato
}


//-----------------------------PING-----------------------------------------------------------------------------------

int handlePong(int sd, MessageHeader* header, RoutingEntry* routingTable) {
    PongPayload pongPayload;
    int index = ricercaDuplicato(routingTable, ntohs(header->id));
    if(index < 0) {
        printf("Errore nella ricerca nella tabella di routing.\n");
        return -1; // errore nella ricerca del duplicato
    }

    int s = recv(sd, &pongPayload, sizeof(PongPayload), 0); 
    if(s <= 0) {
        perror("Errore nella ricezione del pong");
        return -1; // errore nella ricezione del pong
    }

    if(routingTable[index].sockfd == -1) { //ho fatto io la ping questa è la risposta
        printf("Client sulla rete ip:%s:%d ha risposto al mio ping con un pong.\n", pongPayload.ip, ntohs(pongPayload.port));
        return 0; // non inoltro il pong, è una risposta al mio ping
    }

    if(send(routingTable[index].sockfd, header, sizeof(MessageHeader), 0) <0) {
        perror("Errore nell'invio del pong al peer");
        return -1; // errore nell'invio del pong

    } // inoltro il pong al peer che ha inviato il ping
    if(send(routingTable[index].sockfd, &pongPayload, sizeof(PongPayload), 0) < 0) {
        perror("Errore nell'invio del payload del pong al peer");
        return -1; // errore nell'invio del payload del pong
    }
    return 0; // pong gestito con successo    
}


int rispondiPing (int sd, RoutingEntry* routingTable, MessageHeader* header, Peer* outgoing_peers, Peer* incoming_peers, struct sockaddr_in peer_addr) {
    // Funzione per rispondere a un ping
    MessageHeader responseHeader;
    PongPayload pongPayload;
    int j=0;
    if(ntohs(header->type) != TYPE_PING) {
        return -1; // non è un ping
    }
    for(j = 0; j < MAXMESSAGE; j++) {
        if(routingTable[j].sockfd== 0) {
            break; // trovo il primo slot libero nella tabella di routing
        }
    }
    if(j == MAXMESSAGE) {
        printf("Tabella di routing piena, impossibile rispondere al ping.\n");
        return -1; // tabella di routing piena
    }
    if(ricercaDuplicato(routingTable, ntohs(header->id))>=0) {
        printf("Trovato duplicato nella tabella di routing, non rispondo al ping.\n");
        return -1; // trovato duplicato
    }


    // Rispondo con un pong
    responseHeader.type = htons(TYPE_PONG); // tipo di messaggio pong
    responseHeader.payload_length = htons(sizeof(PongPayload)); // lunghezza del payload
    responseHeader.ttl = htons(10); // Time To Live in network byte order
    responseHeader.id = header->id; //rispondo con lo stesso ID del ping gia in formato network byte order
    pongPayload.port = peer_addr.sin_port; // mia porta di ascolto 
    strncpy(pongPayload.ip, LOCALHOST, MAXLEN); // indirizzo IP del peer perchè tanto è una demo basterebbe sostituire con l'ip reale della bind

    if(send(sd, &responseHeader, sizeof(responseHeader), 0)<0){
        perror("Errore nell'invio del pong al peer\n");
        return -1; // errore nell'invio del pong
    }
    if(send(sd, &pongPayload, sizeof(pongPayload), 0) <0){
        perror("Errore nell'invio del payload del pong al peer\n");
        return -1; // errore nell'invio del payload del pong
    }
    // Aggiungo l'ID e il socket alla tabella di routing
    routingTable[j].sockfd = sd; // memorizzo il socket
    routingTable[j].id = ntohs(header->id); // memorizzo l'ID del messaggio

    //inoltro a tutti il ping
    header->ttl = htons(ntohs(header->ttl) - 1); // decremento il TTL
    if(ntohs(header->ttl) <= 0) {
        printf("TTL scaduto per il ping, non inoltro.\n");
        return 0; // TTL scaduto, non inoltro
    }
    //inoltro il ping a tutti i peer
    for(int i = 0; i < MAXOUTGOING; i++) {
        if(outgoing_peers[i].active) {
            if(outgoing_peers[i].sd == sd) {
                continue; // non inoltro a chi me lo ha inviato
            }
            if(send(outgoing_peers[i].sd, header, sizeof(MessageHeader), 0) < 0) {
                perror("Errore nell'invio del ping al peer in uscita");
            }
        }
    }
    for(int i = 0; i < MAXINCOMING; i++) {
        if(incoming_peers[i].active) {
            if(incoming_peers[i].sd == sd) {
                continue; // non inoltro a chi me lo ha inviato
            }
            if(send(incoming_peers[i].sd, header, sizeof(MessageHeader), 0) < 0) {
                perror("Errore nell'invio del ping al peer in arrivo");
            }
        }
    }
    return 1; // risposta inviata con successo
}

int ping(Peer* outgoing_peers, Peer*incoming_peers, RoutingEntry* routingTable) {
    // Funzione per inviare un ping a un peer
    int i,j,count = 0;
    MessageHeader header;
    header.type = htons(TYPE_PING);
    header.payload_length = htons(0); // nessun payload per il ping
    header.ttl = htons(10); // Time To Live in network byte order
    header.id = htons(rand()); // genera ID numerico random

    //controllo di avere spazio nella tabella di routing
    for (j = 0; j < MAXMESSAGE; j++){
        if(routingTable[j].sockfd == 0) { // se il socket è libero
            break; // esce dal ciclo dopo aver trovato uno slot libero
        }
    }
    if(j == MAXMESSAGE) {
        printf("Tabella di routing piena, impossibile inviare ping.\n");
        return -1; // tabella di routing piena
    }

    for(i = 0; i < MAXOUTGOING; i++) {
        if(outgoing_peers[i].active) {
            if(send(outgoing_peers[i].sd, &header, sizeof(header), 0) < 0) {// se non riesco a inviare il ping
                perror("Errore nell'invio del ping al peer in uscita");
            }
            else{
                count++; // conta i peer attivi
            }
        }
    }
    for(i = 0; i < MAXINCOMING; i++) {
        if(incoming_peers[i].active) {
            if(send(incoming_peers[i].sd, &header, sizeof(header), 0) < 0) {// se non riesco a inviare il ping
                perror("\nErrore nell'invio del ping al peer in arrivo\n");
            }
            else{
                count++; // conta i peer attivi
            }
            
        }
    }   
    if(count != 0) {           
        routingTable[j].sockfd = -1; // inizializza il socket a -1
        routingTable[j].id= ntohs(header.id); // memorizza l'ID del messaggio      
    } 
    return count; // ritorna il numero di peer attivi
}



//----------------------------------------------------------QUERY------------------------------------------------------------

int query(Peer* outgoing_peers, Peer* incoming_peers, RoutingEntry* routingTable) {
    // Funzione per inviare una query a un peer
    int i, j, count = 0;
    MessageHeader header;
    QueryPayload queryPayload;


    //ricerco uno slot libero nella tabella di routing
    for(j = 0; j < MAXMESSAGE; j++) {
        if(routingTable[j].sockfd == 0) { // se il socket è libero
            break; // esce dal ciclo dopo aver trovato uno slot libero
        }
    }
    if(j == MAXMESSAGE) {
        printf("Tabella di routing piena, impossibile inviare query.\n");
        return -1; // tabella di routing piena
    }


    // Chiede all'utente di inserire la velocità minima richiesta e il nome del file da cercare
    printf("inserire velocità minima richiesta (in kbps): ");
    scanf("%d", &queryPayload.minimum_speed);
    while(getchar() !='\n'); // pulisco il buffer
    if(queryPayload.minimum_speed < 0) {
        printf("Velocità minima richiesta non valida.\n");
        return -1; // velocità minima non valida
    }

    printf("inserire il nome del file cercato: ");
    scanf("%s", queryPayload.query);
    while(getchar() !='\n'); // pulisco il buffer 

    header.type = htons(TYPE_QUERY);
    header.payload_length = htons(sizeof(QueryPayload)); // lunghezza del payload
    header.ttl = htons(10); // Time To Live in network byte order
    header.id = htons(rand()); // genera ID numerico random 
    queryPayload.minimum_speed = htons(queryPayload.minimum_speed); // converto la velocità minima in network byte order

    //inoltro la query a tutti i peer connessi a me
    for(i = 0; i < MAXOUTGOING; i++) {
        if(outgoing_peers[i].active) {
            if(send(outgoing_peers[i].sd, &header, sizeof(header), 0) < 0) {
                perror("Errore nell'invio della query al peer in uscita");
            } else { // andato a buon fine l'header
                if(send(outgoing_peers[i].sd, &queryPayload, sizeof(queryPayload), 0) < 0) {
                    perror("Errore nell'invio del payload della query al peer in uscita");
                }
                else{//andato a buon fine il payload
                    count++; // conta i peer attivi
                }
            }          
        }
    }
    
    for(i = 0; i < MAXINCOMING; i++) {
        if(incoming_peers[i].active) {
            if(send(incoming_peers[i].sd, &header, sizeof(header), 0) < 0) {
                perror("Errore nell'invio della query al peer in arrivo");
            } else { // andato a buon fine l'header
                if(send(incoming_peers[i].sd, &queryPayload, sizeof(queryPayload), 0) < 0) {
                    perror("Errore nell'invio del payload della query al peer in arrivo");
                }
                else{//andato a buon fine il payload
                    count++; // conta i peer attivi
                }
            }
            
        }
    }
    if(count == 0){
        printf("Nessun peer attivo per la query.\n");
        return 0; // nessun peer attivo
    }
    routingTable[j].sockfd = -1; // inizializza il socket a -1
    routingTable[j].id = ntohs(header.id); // memorizza l'ID
    return count; // ritorna il numero di peer attivi
}


int handleQuery(int sd, MessageHeader* header, Peer* outgoing_peers, Peer* incoming_peers, RoutingEntry* routingTable, int mySpeed, struct sockaddr_in bind_ip_port, int listenPort, char fs[NMAXFILE][MAXLEN]) {
    QueryPayload queryPayload;
    query_hit_payload queryHitPayload;
    MessageHeader hitResponseHeader;

    int j,k,s,count = 0;
    if(ntohs(header->type) != TYPE_QUERY) {
        printf("Ricevuto un messaggio non di tipo query.\n");
        return -1; // non è una query
    }

    int index = ricercaDuplicato(routingTable, ntohs(header->id));
    if(index >= 0) {
        printf("Errore nella ricerca del duplicato nella tabella di routing.\n");
        return -1; // errore nella ricerca del duplicato
    }
    s = recv(sd, &queryPayload, sizeof(QueryPayload), 0);
    if(s <1){
        perror("Errore nella ricezione del payload della query");
        return -1; // errore nella ricezione del payload della query
    }
    //ricerco in locale
    if(ntohs(queryPayload.minimum_speed) <= mySpeed) {
        queryHitPayload.n_hits =0;
        queryHitPayload.port = htons(listenPort);
        queryHitPayload.speed = htons(mySpeed);
        strncpy(queryHitPayload.ip, inet_ntoa(bind_ip_port.sin_addr), MAXLEN);

        //logica di ricerca dei file nel file system
        for (int i = 0; i < NMAXFILE; i++) {
            if (strlen(fs[i]) == 0) continue;
            // confronto semplice: la query è contenuta nel nome del file
            if (strstr(fs[i], queryPayload.query) != NULL) {//scorro il file system a trovare i file che contengono la query
                strncpy(queryHitPayload.results[queryHitPayload.n_hits].name, fs[i], MAX_FILENAME); // copio il nome del file nel payload
                queryHitPayload.results[queryHitPayload.n_hits].index = htons(i);
                queryHitPayload.n_hits++;
                if (queryHitPayload.n_hits >= MAX_RESULTS){
                    break; // raggiunto il numero massimo di risultati del payload dovra essere piu specifico il client
                }
            }
        }

        if (queryHitPayload.n_hits > 0) {
            // Costruzione header risposta
            hitResponseHeader.type = htons(TYPE_QUERYHIT); // tipo di messaggio QUERY_HIT
            hitResponseHeader.ttl = htons(10); // Time To Live in network byte order
            hitResponseHeader.payload_length = htons(sizeof(query_hit_payload)); // lunghezza del payload
            hitResponseHeader.id = header->id;  // stesso ID della query ricevuta
            queryHitPayload.n_hits = htons(queryHitPayload.n_hits); // converto il numero di risultati in network byte order

            // Invia prima l’header
            if(send(sd, &hitResponseHeader, sizeof(MessageHeader), 0)< 0){
                perror("Errore nell'invio dell'header di query hit");
                return -1;
            }

            // Poi invia il payload
            if(send(sd, &queryHitPayload, sizeof(query_hit_payload), 0) < 0) {
                perror("Errore nell'invio del payload di query hit");
                return -1;
            }
            printf("Risposta QUERY_HIT inviata con %d risultati.\n", ntohs(queryHitPayload.n_hits));
        }

    }

    for(k = 0; k < MAXMESSAGE; k++) {//messo qui perchè se la tabella di routing è piena non inoltro la query ma rispondo con un queryhit se la ho
        if(routingTable[k].sockfd== 0) {
            break; // trovo il primo slot libero nella tabella di routing
        }
    }
    if(k == MAXMESSAGE) {
        printf("Tabella di routing piena, impossibile inoltrare query.\n");
        return -1; // tabella di routing piena
    }

    //controllo ttl
    header->ttl = htons(ntohs(header->ttl) - 1); // decrement
    if(ntohs(header->ttl) <= 0) {
        printf("TTL scaduto per la query, non inoltro.\n");
        return 0; // TTL scaduto, non inoltro
    }


    printf("Inoltro della query a tutti i peer attivi.\n");
    for(j = 0; j < MAXOUTGOING; j++) {
        if(outgoing_peers[j].active) {
            if(outgoing_peers[j].sd == sd) {
                continue; // non inoltro a chi me lo ha inviato
            }
            if(send(outgoing_peers[j].sd, header, sizeof(MessageHeader), 0) < 0) {
                perror("Errore nell'invio della query al peer in uscita");
            } else { // andato a buon fine l'header
                if(send(outgoing_peers[j].sd, &queryPayload, sizeof(QueryPayload), 0) < 0) {
                    perror("Errore nell'invio del payload della query al peer in uscita");
                } else {
                    count++; // conta i peer attivi
                }
            }
        }
    }
    for(j = 0; j < MAXINCOMING; j++) {
        if(incoming_peers[j].active) {
            if(incoming_peers[j].sd == sd) {
                continue; // non inoltro a chi me lo ha inviato
            }
            if(send(incoming_peers[j].sd, header, sizeof(MessageHeader), 0) < 0) {
                perror("Errore nell'invio della query al peer in arrivo");
            } else { // andato a buon fine l'header
                if(send(incoming_peers[j].sd, &queryPayload, sizeof(QueryPayload), 0) < 0) {
                    perror("Errore nell'invio del payload della query al peer in arrivo");
                } else {
                    count++; // conta i peer attivi
                }
            }
        }
    }
    if(count == 0) {
        printf("Nessun peer attivo per la query.\n");
        return 0; // nessun peer attivo
    }
    routingTable[k].sockfd = sd; // segno quale socket ha inviato la query
    routingTable[k].id = ntohs(header->id); // memorizza l'ID

    return count; // ritorna il numero di peer attivi
}



int handleQueryHit(int sd, MessageHeader* header, RoutingEntry* routingTable) {
    // Funzione per gestire una risposta di tipo QUERY_HIT
    query_hit_payload queryHitPayload;
    int s;

    if(ntohs(header->type) != TYPE_QUERYHIT) {
        printf("Ricevuto un messaggio non di tipo QUERY_HIT.\n");
        return -1; // non è un QUERY_HIT
    }

    if(recv(sd, &queryHitPayload, sizeof(query_hit_payload), 0) < 0) {
        perror("Errore nella ricezione del payload di QUERY_HIT");
        return -1; // errore nella ricezione del payload di QUERY_HIT
    }


    printf("Ricevuto QUERY_HIT con %d risultati:\n", ntohs(queryHitPayload.n_hits));
    s=ricercaDuplicato(routingTable, ntohs(header->id));
    if(s < 0) {
        printf("Errore nella ricerca nella tabella di routing.\n");
        return -1; // errore nella ricerca del duplicato
    }
    if(routingTable[s].sockfd == -1) {//sono io che ho fatto la query
        printf("Ricevuto QUERY_HIT per la mia query  da %s:%d\n", queryHitPayload.ip, ntohs(queryHitPayload.port));
        for(int i = 0; i < ntohs(queryHitPayload.n_hits); i++) {
            printf("Risultato %d: %s (indice: %d)\n", i + 1, queryHitPayload.results[i].name, ntohs(queryHitPayload.results[i].index));
        }
        return 0; // gestione del QUERY_HIT completata con successo
    }

    //inoltro la queryHIT a colui che ha fatto la query
    if(send(routingTable[s].sockfd, header, sizeof(MessageHeader), 0) < 0) {
        perror("Errore nell'invio dell'header di QUERY_HIT al peer");
        return -1; // errore nell'invio dell'header di QUERY_HIT
    }
    if(send(routingTable[s].sockfd, &queryHitPayload, sizeof(query_hit_payload), 0) < 0) {
        perror("Errore nell'invio del payload di QUERY_HIT al peer");
        return -1; // errore nell'invio del payload di QUERY_HIT
    }    
    printf("inoltrata query hit \n");
    return 0; // gestione del QUERY_HIT completata con successo
}


void die(char *error) {
	fprintf(stderr, "%s.\n", error);
	exit(1);
	
}