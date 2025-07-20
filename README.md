Relazione progretto: Client rete P2P GNUTELLA-LIKE
Descrizione progetto:
Il progetto riguarda l’implementazione di una rete Peer-to-Peer (P2P) ispirata al protocollo Gnutella.
Ogni nodo della rete è un servent (un peer che funge sia da client che da server), e non è previsto alcun nodo centrale o superpeer.
I messaggi scambiati tra i peer sono quelli standard di Gnutella: PING, PONG, QUERY e QUERYHIT. Lo scambio dei messaggi avviene tramite flooding, ovvero i messaggi vengono inoltrati a tutti i peer connessi (eccetto quello da cui si è ricevuto il messaggio).
Il meccanismo di bootstrap avviene tramite un well-known host: l’utente inserisce manualmente un indirizzo IP e una porta a cui connettersi all’avvio.
In alternativa, è possibile estendere il progetto per utilizzare un file di testo come registro dei peer attivi, aggiornato automaticamente in seguito alla ricezione di messaggi PING.
Per simulare il file system condiviso, ogni servent mantiene una lista statica di file disponibili, identificati da un nome e un indice. Nella versione reale di Gnutella, a seguito di un messaggio QUERYHIT, il download dei file avviene tramite connessione HTTP diretta; nel mio progetto, per motivi di tempo, il trasferimento vero e proprio dei file non è stato implementato.
L’obiettivo principale del progetto è stato comprendere e simulare le dinamiche di una rete decentralizzata P2P, con particolare attenzione alla gestione dei messaggi, alla tabella di routing, e alla ricerca distribuita di contenuti.
