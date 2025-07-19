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

//struttura pongPayload port int e string ip
//str

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



void die(char *);
void stampaPeer(Peer* incoming_peers, Peer* outgoing_peers);



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
    //---------------------------------------------------------fine inizializzazione------------------------------------------------------------

    //
    // Ciclo principale del server
    //
    printf("inserire well konwn peer\n");
    if(connectToPeer(LOCALHOST,  outgoing_peers, &maxfd, &readFDSET) < 0) {
        printf("Errore nella connessione al peer well known");
    }
    
    //menu utente testuale
    printf("\n\nMenu:\n 1. Esegui un ping \n  2. esegui una query \n 3. aggiungi peer \n 4. listPeer \n 5. esci\n");



    // Ciclo principale per gestire le connessioni e le comunicazioni
    while (flag){
        memcpy(&temp, &readFDSET, sizeof(temp)); //copio il set di lettura per la select dentro temp che verra modificato da select
        n= select(maxfd+1, &temp, NULL, NULL, &time); //attendo eventi sui socket
        
        if(n>0){    //almeno un socket è pronto
            printf("\n%d socket pronti\n", n);

            
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
                        //@TODO: implementare la logica della query
                        break;
                    
                    case 3:     //aggiungi peer
                        printf("Aggiungi peer...\n");
                        if(connectToPeer(LOCALHOST, outgoing_peers, &maxfd, &readFDSET)<0) {
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
                        printf("Uscita dal server...\n");
                        flag = 0; //esce dal ciclo principale
                        chiudiConnessioni(incoming_peers, outgoing_peers);
                        close(listenSocket); //chiude il socket di ascolto
                        break;
                }  
                

            }
            if(!flag){
                break; //esce dal ciclo principale se l'utente ha scelto di uscire
            }

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
                    } 
                    if(p < 0) {
                        printf("Errore nella ricezione del messaggio dal peer in uscita.\n");
                        continue; // continua il ciclo se c'è un errore
                    }
                    else{
                        // Gestisco il messaggio ricevuto
                        switch(header.type) {
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
                                //@todo
                                break;
                            
                            case TYPE_QUERYHIT:
                                //@todo
                                break;
                        }
                    }
                }
            }
            
            //controllo se ci sono peer in arrivo pronti
            for(int i = 0; i < MAXINCOMING; i++) {
                if(incoming_peers[i].active && FD_ISSET(incoming_peers[i].sd, &temp)) {
                    //@TODo: gestire i peer in arrivo
                }
            }
            //menu utente testuale
            printf("\n\nMenu:\n 1. Esegui un ping \n 2. esegui una query \n 3. aggiungi peer \n 4. listPeer \n 5. esci\n");
        }
    }
}


int riceviMessaggio(int sd, MessageHeader* header) {
    // Funzione per ricevere un messaggio da un peer
    int s
    s=recv(sd, header, sizeof(MessageHeader), 0);
    if(s == 0) {
        printf("connessione chiusa dal peer.\n");
        return 0; // connessione chiusa dal peer
    }
    if(s < 0) {
        perror("Errore nella ricezione del messaggio");
        return -1; // errore nella ricezione del messaggio
    }

    return 0; // messaggio ricevuto con successo
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



int connectToPeer(char *ip, Peer* outgoing_peers, int* maxfd, fd_set* readFDSET) {
    int port, i;
    struct sockaddr_in peer_addr;
    // Chiede all'utente di inserire la porta del peer
    printf("inserire porta del peer\n");
    scanf("%d", &port);
    while(getchar() !='\n'); // pulisco il buffer
    while(port < 1024 || port > 65535) {
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


int ricercaDuplicato(RoutingEntry* routingTable, int id, int sockfd) {
    // Funzione per cercare un duplicato nella tabella di routing
    for(int i = 0; i < MAXMESSAGE; i++) {
        if(routingTable[i].id == id) {
            return i; // trovato duplicato
        }
    }
    return 0; // nessun duplicato trovato
}

int handlePong(int sd, MessageHeader* header, RoutingEntry* routingTable) {
    PongPayload pongPayload;
    int index = ricercaDuplicato(routingTable, ntohs(header->id), sd);
    if(index < 0) {
        printf("Errore nella ricerca del duplicato nella tabella di routing.\n");
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


int rispondiPing (int sd, RoutingEntry* routingTable, MessageHeader* header, Peer* outgoing_peers, Peer* incoming_peers, sockaddr_in peer_addr) {
    // Funzione per rispondere a un ping
    MessageHeader responseHeader;
    PongPayload pongPayload;
    int j;
    if(header->type != TYPE_PING) {
        return -1; // non è un ping
    }
    for(j = 0; j < MAXMESSAGE; j++) {
        if(routingTable[j].sockfd== 0) {
            break; // trovo il peer in uscita
        }
    }
    if(j == MAXMESSAGE) {
        printf("Tabella di routing piena, impossibile rispondere al ping.\n");
        return -1; // tabella di routing piena
    }
    if(ricercaDuplicato(routingTable, ntohs(header->id), sd)>0) {
        printf("Trovato duplicato nella tabella di routing, non rispondo al ping.\n");
        return -1; // trovato duplicato
    }


    // Rispondo con un pong
    responseHeader.type = TYPE_PONG;
    responseHeader.payload_length = htons(sizeof(PongPayload)); // lunghezza del payload
    responseHeader.ttl = htons(10); // Time To Live in network byte order
    responseHeader.id = header->id; //rispondo con lo stesso ID del ping
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
    routingTable[j].id = header->id; // memorizzo l'ID del messaggio

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
    header.type = TYPE_PING;
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




void die(char *error) {
	
	fprintf(stderr, "%s.\n", error);
	exit(1);
	
}