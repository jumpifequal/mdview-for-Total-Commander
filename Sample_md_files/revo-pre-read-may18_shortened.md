Le 7 regole architetturali

1. Threat modeling source → sink
   Mappare tutti i percorsi attraverso cui dati controllabili da un attaccante possono raggiungere sistemi privilegiati. Se un percorso critico esiste, va spezzato.

2. Segregazione dati fidati / non fidati
   Nessun componente AI con privilegi elevati deve ricevere input che un attaccante possa influenzare. Se i flussi devono incrociarsi, l'intersezione avviene solo attraverso un layer di mediazione dedicato.

3. Mediazione su ogni azione critica
   Il modello non interagisce mai direttamente con sistemi privilegiati. Ogni azione critica transita attraverso un broker che valida, filtra e autorizza — con policy engine, allowlist, step-up approval e logging per audit.

4. Privilegi dinamici (capability shifting)
   I privilegi del modello si adattano in tempo reale al livello di fiducia degli input. Quando entra un dato non fidato, il sistema scala automaticamente i privilegi verso il basso.

5. RAG e vector store come asset di prima classe
   Gli indici RAG non sono accessori: sono database sensibili e canali di injection. Richiedono segregazione per livello di trust, validazione in ingestion e monitoraggio continuo.

6. Guardrail come defense-in-depth, non come controllo primario
   I guardrail (system prompt difensivi, filtri, classificatori) sono utili ma non deterministici e bypassabili. Il system prompt non è un confine di sicurezza — non è confidenziale né immutabile. Non può essere l'unico controllo.

7. AI Red Teaming basato sul threat model
   Test automatizzati, ripetibili, eseguiti periodicamente e a ogni cambio significativo. Copertura su quattro livelli: applicazione, modello, data plane, infrastruttura. I test falliti sono alert di sicurezza.
