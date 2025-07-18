#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
    char id[16];
    int sockfd; // -1 se sono io il mittente originale
} RoutingEntry;

typedef struct Peer {
    int sd;
    struct sockaddr_in addr;
    int active; // 0 = slot libero, 1 = usato
} Peer;

typedef struct MessageHeader {
    int type; // 1 per ping, 2 per query, 3 pong, 4 queryhit
    char id[16]; // ID univoco (puoi usare 16 byte random)
    int ttl;
    int payload_length; // lunghezza del payload
} MessageHeader;



void die(char *);
void stampaPeer(Peer* incoming_peers, Peer* outgoing_peers);



// Funzione di utilità per leggere un intero da stdin in modo sicuro
int leggiIntero(const char* prompt, int min, int max) {
    char buffer[64];
    int n;
    while (1) {
        printf("%s", prompt);
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("Errore o EOF su input.\n");
            return min-1; // valore impossibile
        }
        if (sscanf(buffer, "%d", &n) == 1 && n >= min && n <= max) {
            return n;
        }
        printf("Valore non valido. Deve essere compreso tra %d e %d.\n", min, max);
    }
}

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
    listenPort = leggiIntero("Porta: ", 1024, 65535);
    if (listenPort < 1024) die("Errore nella lettura della porta di ascolto");

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
    
    

    // Ciclo principale per gestire le connessioni e le comunicazioni
    while (flag){

        //menu utente testuale
        printf("\n\nMenu:\n 1. Esegui un ping \n  2. esegui una query \n 3. aggiungi peer \n 4. listPeer \n 5. esci\n");


        memcpy(&temp, &readFDSET, sizeof(temp)); //copio il set di lettura per la select dentro temp che verra modificato da select
        n= select(maxfd+1, &temp, NULL, NULL, &time); //attendo eventi sui socket
        
        if(n>0){    //almeno un socket è pronto
            printf("\n%d socket pronti\n", n);
            if(FD_ISSET(STDIN_FILENO, &temp)){  //è Pronto lo stdin
                scelta = leggiIntero("Scelta: ", 1, 5);
                if (scelta < 1) {
                    printf("Input non valido o EOF.\n");
                    continue;
                }

                switch (scelta) {
                    case 1: //fa il ping
                        printf("Eseguo ping...\n");
                        /*if(outgoing_peers[0].active) {
                            char ping_msg[] = "PING";
                            send(outgoing_peers[0].sd, ping_msg, sizeof(ping_msg), 0);
                            printf("Ping inviato al peer well known.\n");
                        } else {
                            printf("Peer well known non attivo.\n");
                        }*/
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
                    //@TODO: gestire i peer in uscita
                }
            }
            
            //controllo se ci sono peer in arrivo pronti
            for(int i = 0; i < MAXINCOMING; i++) {
                if(incoming_peers[i].active && FD_ISSET(incoming_peers[i].sd, &temp)) {
                    //@TODo: gestire i peer in arrivo
                }
            }
        }
    }

	
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
    port = leggiIntero("inserire porta del peer\nPorta: ", 1024, 65535);
    if (port < 1024) {
        printf("Errore nella lettura della porta del peer.\n");
        return -1;
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

void die(char *error) {
	
	fprintf(stderr, "%s.\n", error);
	exit(1);
	
}
