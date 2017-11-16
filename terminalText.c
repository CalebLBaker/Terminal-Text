/* Terminal Text
 * Author:	Caleb Baker
 * Date:	November 16, 2017
 * A simple app to make texting from the termux app for android easier.
 */


#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>
#include <pthread.h>

#define BOOL unsigned char
#define TRUE 1
#define FALSE 0

#define DEFAULT_TEXT_SIZE 160
#define DEFAULT_NAME_SIZE 16
#define DEFAULT_TIME_SIZE 16
#define DEFAULT_NUMBER_SIZE 10

#define MAX_OUTGOING_TEXT_SIZE 160
#define MAX_TEXT_PLUS_ONE 161
#define COMMAND_LENGTH 28
#define BASE_COMMAND_LENGTH 18

#define JUNK_SIZE 64

#define DEFAULT_QUEUE_SIZE 8

#define RESCALING_FACTOR 1.5

#define MAX_LOAD_FACTOR 0.75
#define HASH_MAP_RESCALING_FACTOR 1.5
#define HASH_MAP_START_RATIO 1.5
#define MAP_FULL -0x80000000

#define STDIN 1

#define MAGIC_HASH_NUMBER 5381
#define OTHER_MAGIC_HASH_NUMBER 5

// Struct for storing contact information.
typedef struct {
    char *name;
    char *number;
    unsigned nameSize;
    unsigned numberSize;
} contact;


// Struct for storing a message.
typedef struct {
    char *text;
    char *name;
    char *number;
    char *time;
    unsigned textSize;
    unsigned nameSize;
    unsigned timeSize;
    unsigned numberSize;
    BOOL inContacts;
} message;


// Struct to hold a message queue.
typedef struct {
    unsigned capacity;
    unsigned size;
    unsigned front;
    unsigned back;
    message *data;
} queue;


queue q;

pthread_mutex_t qLock;


// Function for initializing a message struct.
void initializeMessage(message *x) {
    x->text = (char*) malloc(DEFAULT_TEXT_SIZE * sizeof(char));
    x->name = (char*) malloc(DEFAULT_NAME_SIZE * sizeof(char));
    x->number = (char*) malloc(DEFAULT_NUMBER_SIZE * sizeof(char));
    x->time = (char*) malloc(DEFAULT_TIME_SIZE * sizeof(char));
    x->textSize = DEFAULT_TEXT_SIZE;
    x->nameSize = DEFAULT_NAME_SIZE;
    x->numberSize = DEFAULT_NUMBER_SIZE;
    x->timeSize = DEFAULT_TIME_SIZE;
}


// Read f into str. Terminated by \". Returns new str.
char* getString(char *str, unsigned *size, FILE *f, char delimiter) {
    int i = 0;
    char *st = str;
    unsigned s = *size;
    // Read to the string.
    do {
	char in = (char) fgetc(f);
	
	// Resize the string if necessary
	if (i == s) {
		s = (unsigned) (s * RESCALING_FACTOR);
		if (s < 2) {
		    s++;
		}
		st = (char*) realloc(st, s);
	}

	// If we are reading from stdin then we need to convert " to \"
	if (delimiter == '\n') {
		if (in == '\"') {
			st[i++] = '\\';
			st[i++] = '\"';
			in = (char) fgetc(f);
		}
	}

	// If we are reading from termux-sms-inbox then we need to handle excape characters.
	else {
		while (in == '\\') {
			in = (char) fgetc(f);
			switch (in) {
				case 'n':
					st[i] = '\n';
					break;
				case 't':
					st[i] = '\t';
					break;
				default:
					st[i] = in;
			}
			i++;
			in = (char) fgetc(f);
		}
	}

	st[i++] = in;	// Now we can read a character.

    } while (st[i - 1] != delimiter);

    // Null terminate, adjust size, and return.
    st[i - 1] = '\0';
    *size = s;
    return st;
}


// Deep copies one message to another.
void copy(message *source, message *destination) {
    // Make sure using strcpy is safe.
    if (source->textSize > destination->textSize) {
	destination->text = (char*) realloc(destination->text, source->textSize);
	destination->textSize = source->textSize;
    }
    if (source->nameSize > destination->nameSize) {
	destination->name = (char*) realloc(destination->name, source->nameSize);
	destination->nameSize = source->nameSize;
    }
    if (source->timeSize > destination->timeSize) {
	destination->time = (char*) realloc(destination->time, source->timeSize);
	destination->timeSize = source->timeSize;
    }
    if (source->numberSize > destination->numberSize) {
	destination->number = (char*) realloc(destination->number, source->numberSize);
	destination->numberSize = source->numberSize;
    }
    // Use strcpy
    strcpy(destination->text, source->text);
    strcpy(destination->name, source->name);
    strcpy(destination->time, source->time);
    strcpy(destination->number, source->number);

    destination->inContacts = source->inContacts;
}


// Repeatedly checks inbox and puts new messages in the queue.
void* getMessages(void *args) {
    message newest;		// The newest message that has been processed
    message curr;		// The message currently being processed
    char junk[JUNK_SIZE];	// Array to hold junk data from file input.
    initializeMessage(&newest);
    initializeMessage(&curr);
    newest.time[0] = '\0';	// Make sure the first message is recongnized as new.

    // Main loop
    while (TRUE) {
	// Run inbox command and strip away junk from top of input.
	FILE *f = popen("termux-sms-inbox -l 1", "r");
	fscanf(f, "[\n {\n %[^\n]\n", junk);
        fscanf(f, " \"%[^\"]\": \"", junk);

	// If the next field isn't sender, the number isn't in the contacts.
        if (strcmp(junk, "sender")){
	    curr.inContacts = FALSE;
        }
	// If the number is in the contact, get the name.
        else {
	    curr.inContacts = TRUE;
	    curr.name = getString(curr.name, &curr.nameSize, f, '\"');
	    fscanf(f, ",\n \"number\": \"");
        }
	// Get the number.
	curr.number = getString(curr.number, &curr.numberSize, f, '\"');
	// Get the time stamp.
        fscanf(f, ",\n \"received\": \"");
	curr.time = getString(curr.time, &curr.timeSize, f, '\"');
	// Get the message.
	fscanf(f, ",\n \"body\": \"");
	curr.text = getString(curr.text, &curr.textSize, f, '\"');

	// If curr represents a new message, update newest and insert into messageQueue.
        if(strcmp(newest.time, curr.time) != 0 || strcmp(newest.text, curr.text) != 0){
	    copy(&curr, &newest);

	    pthread_mutex_lock(&qLock);
////////////// MODIFYING QUEUE/////////////////////////////////////////////////
	    // Allocate more memory if queue is full.
	    if (q.size == q.capacity) {
		unsigned newCap = (unsigned) q.capacity * RESCALING_FACTOR;
		message *newData = (message*) malloc(newCap * sizeof(message));
		memcpy((void*) newData, (void*) (q.data + q.front), (q.capacity - q.front) * sizeof(message));
		memcpy((void*) (newData + q.capacity - q.front), (void*) q.data, q.front * sizeof(message));
		free(q.data);
		q.data = newData;
		q.front = 0;
		q.back = q.size;
		q.capacity = newCap;
	    }
	    
	    // Insert into queue
	    copy(&curr, q.data + q.back);
	    q.back++;
	    q.back %= q.capacity;
	    q.size++;
	    pthread_mutex_unlock(&qLock);
////////////// DONE MODIFYING QUEUE////////////////////////////////////////////
	}
        pclose(f);
    }
    return NULL;
}


// Hashes str and mods by size. Returns the result.
unsigned hash (char *str, unsigned size) {
    unsigned long hash = MAGIC_HASH_NUMBER;
    int c;
    while ((c = *str++)) {
	hash = ((hash << OTHER_MAGIC_HASH_NUMBER) + hash) + c;
    }
    return (unsigned) (hash % size);
}


// Finds the index of key in map of size size.
// Compares to name if byName, else compares to number.
// Returns the index if found.
// Returns negative index of insertion point  - 1 if there is room to insert.
// Returns MAP_FULL if map is full and key is absent.
int findIndex(contact **map, char *key, unsigned size, BOOL byName) {

    int h = hash(key, size);

    for (unsigned i = 0; i < size; i++) {
	int index = (h + i) % size;
        if (map[index] == NULL) {
	    return -index - 1;
	}
	else {
	    char *comparison = map[index]->number;
	    if (byName) {
	        comparison = map[index]->name;
	    }
	    if (!strcmp(comparison, key)) {
	        return index;
	    }
	}
    }
    return MAP_FULL;
}


void sendMessage(char *outBuffer, char *command, char *number, unsigned outBufferSize) {
    // out is the outgoing text message.
    outBuffer = getString(outBuffer, &outBufferSize, stdin, '\n');
    char *out = outBuffer;
    // Set up the initial command.
    command[BASE_COMMAND_LENGTH] = '\0';
    strcat(command, number);
    strcat(command, " \"");

    // Record the size of command and out.
    int size = strlen(out);
    int commandSize = strlen(command);

    unsigned numTexts = 1;

    // If the text is too long to send all at once:
    while (size > MAX_OUTGOING_TEXT_SIZE) {
        // Copy the first 160 characters into the command and send.
        memcpy((void*) (command + commandSize), out, MAX_OUTGOING_TEXT_SIZE);
        command[commandSize + MAX_OUTGOING_TEXT_SIZE] = '\"';
        command[commandSize + MAX_TEXT_PLUS_ONE] = '\0';
        system(command);
	// Set things up for the regular stuff to send the second half
        out += MAX_OUTGOING_TEXT_SIZE;
	size -= MAX_OUTGOING_TEXT_SIZE;
	command[commandSize] = '\0';
	numTexts++;
    }

    // Put the command fully together and send.
    strcat(command, out);
    strcat(command, "\"");
    system(command);
    if (numTexts > 1) {
        printf("Text sent in %u parts.\n", numTexts);
    }
}


// Takes all of the entries in contacts array and inserts them into nameToNum and numToName hash maps.
void fillHashMaps(contact *contacts, contact **nameToNum, contact **numToName, unsigned numContacts, unsigned hashMapSize) {
    for (unsigned i = 0; i < numContacts; i++) {
        nameToNum[-findIndex(nameToNum, contacts[i].name, hashMapSize, TRUE) - 1] = contacts + i;
	numToName[-findIndex(numToName, contacts[i].number, hashMapSize, FALSE) - 1] = contacts + i;
    }
}


// Saves numContacts from contacts to contactFilename.
void saveContacts(char *contactFilename, contact *contacts, unsigned numContacts) {
    FILE *contactFile = fopen(contactFilename, "w");
    if (contactFile != NULL) {
        fprintf(contactFile, "%u\n", numContacts);
        for (unsigned i = 0; i < numContacts; i++) {
            fprintf(contactFile, "%s %s\n", contacts[i].name, contacts[i].number);
        }
        printf("%u contacts saved.\n\n\n", numContacts);
    }
    fclose(contactFile);
}


int main(int argc, char **argv){

    char *contactFilename;
    char *contactName = NULL;
    unsigned nameLen = 0;

    // Open contact file.
    if (argc == 1) {
	contactFilename = malloc(13 * sizeof(char));
	strcpy(contactFilename, "contacts.txt");
    }
    else {
	contactFilename = argv[1];
    }
    FILE *contactFile = fopen(contactFilename, "r");

    // Get the number of contacts.
    unsigned numContacts = 0;
    if (contactFile != NULL) {
        fscanf(contactFile, "%u\n", &numContacts);
    }

    // Declare variables to store contact information.
    unsigned hashMapSize = 0;
    contact **nameToNum = NULL;
    contact **numToName = NULL;
    contact *contacts = NULL;

    // Allocate memory.
    if (numContacts) {
        hashMapSize = numContacts * HASH_MAP_START_RATIO;
	nameToNum = calloc(hashMapSize, sizeof(contact*));
	numToName = calloc(hashMapSize, sizeof(contact*));
	contacts = calloc(hashMapSize, sizeof(contact));
    }

    // Insert all of the contacts into array and hash maps.
    for (unsigned i = 0; i < numContacts; i++) {
	contacts[i].name = getString(contacts[i].name, &contacts[i].nameSize, contactFile, ' ');
	contacts[i].number = getString(contacts[i].number, &contacts[i].numberSize, contactFile, '\n');
    }
    fillHashMaps(contacts, nameToNum, numToName, numContacts, hashMapSize);

    // Close file.
    fclose(contactFile);

    // Set up queue.
    q.data = (message*) malloc(DEFAULT_QUEUE_SIZE * sizeof(message));
    q.capacity = DEFAULT_QUEUE_SIZE;
    q.front = 0;
    q.back = 0;
    q.size = 0;
    for (unsigned i = 0; i < DEFAULT_QUEUE_SIZE; i++) {
	initializeMessage(q.data + i);
    }

    printf("\n\n\n");

    struct pollfd fds;
    fds.fd = STDIN;
    fds.events = POLLIN;

    // Some buffers for sending texts.
    unsigned outBufferSize = MAX_OUTGOING_TEXT_SIZE;
    char *outBuffer = (char*) malloc(MAX_OUTGOING_TEXT_SIZE * sizeof(char));
    char command[COMMAND_LENGTH + DEFAULT_NUMBER_SIZE + MAX_OUTGOING_TEXT_SIZE];
    strcpy(command, "termux-sms-send -n");

    if (pthread_mutex_init(&qLock, NULL) != 0) {
	printf("Mutex initialization failed.\n");
	return 1;
    }

    pthread_t inboxThread;
    pthread_create(&inboxThread, NULL, getMessages, NULL);

    // Main loop
    while (TRUE){

	// Handle user input.
	if (poll(&fds, 1, 0)) {
	    char c = getchar();

	    switch(c) {
	        // Send a text message.
		case 't': {
                    getchar();
                    contactName = getString(contactName, &nameLen, stdin, '\n');

		    // If they enter a phone number
                    if ('0' <= contactName[0] && contactName[0] <= '9') {
		        sendMessage(outBuffer, command, contactName, outBufferSize);
		        printf("\nTexting %s\n\n\n", contactName);
		    }

		    // If they enter a contact name.
		    else {
		        int index = findIndex(nameToNum, contactName, hashMapSize, TRUE);
		        if (index >= 0) {
		            char *number = nameToNum[index]->number;
                            sendMessage(outBuffer, command, number, outBufferSize);
	                    printf("\nTexting %s\n\n\n", number);
		        }
		        else {
		            printf("%s is not in your contacts.\n\n\n", contactName);
		        }
		    }
	            break;
		}

		// Add a contact
		case 'a': {
		    getchar();

		    // Reallocate larger memory for hash maps if necessary.
		    if (numContacts > (unsigned) (MAX_LOAD_FACTOR * hashMapSize)) {
			free(nameToNum);
			free(numToName);
			if (hashMapSize < 2) {
			    hashMapSize++;
			}
			else {
			    hashMapSize *= HASH_MAP_RESCALING_FACTOR;
			}
			contacts = realloc(contacts, hashMapSize * sizeof(contact));
			nameToNum = calloc(hashMapSize, sizeof(contact*));
			numToName = calloc(hashMapSize, sizeof(contact*));
			fillHashMaps(contacts, nameToNum, numToName, numContacts, hashMapSize);
		    }

		    // Set name for new contact.
		    contact *newContact = contacts + numContacts;
		    newContact->name = getString(newContact->name, &newContact->nameSize, stdin, '\n');

		    // Make sure name is valid to insert.
		    if ('0' <= newContact->name[0] && newContact->name[0] <= '9') {
		        printf("%s is not a valid contact name. Names cannot start with digits.\n\n\n", newContact->name);
			break;
		    }
                    int nameIndex = findIndex(nameToNum, newContact->name, hashMapSize, TRUE);
                    if (nameIndex >= 0) {
			printf("%s is already in your contacts.\n\n\n", newContact->name);
		    }
		    else {

			// Get number for contact.
		        newContact->number = getString(newContact->number, &newContact->numberSize, stdin, '\n');
			int numIndex = findIndex(numToName, newContact->number, hashMapSize, FALSE);
			if (numIndex >= 0) {
			    printf("%s is already in your contacts.\n\n\n", newContact->number);
			}

			// Insert.
			else {
                            nameToNum[-nameIndex - 1] = newContact;
			    numToName[-numIndex - 1] = newContact;
			    numContacts++;
			    printf("%s inserted into contacts with number %s.\n\n\n", newContact->name, newContact->number);
			}    
		    }
                    break;
		}

                // Print all contacts
	        case 'p': {
		    for (unsigned i = 0; i < numContacts; i++) {
			printf("%s %s\n", contacts[i].name, contacts[i].number);
		    }
		    printf("\n\n\n");
		    break;
		}

		// Print battery status
	        case 'b': {
		    system("termux-battery-status");
		    printf("\n\n\n");
		    break;
		}

		// Save contacts
		case 'w': {
		    saveContacts(contactFilename, contacts, numContacts);
		    break;
		}

		// Save and exit
	        case 'x': {
		    saveContacts(contactFilename, contacts, numContacts);
		}

	        // Quit
	        case 'q': {
		    return 0;
		}

	        // Print a number of messages from the inbox.
	        case 'i': {
		    unsigned n;
		    scanf("%u", &n);
		    char printInbox[32];
		    sprintf(printInbox, "termux-sms-inbox -l %u", n);
		    system(printInbox);
		    printf("\n\n\n");
		    break;
		}
	    }
	}

	// If the queue is not empty deal with it.
        if(q.size != 0){
	    message curr;
	    initializeMessage(&curr);

	    pthread_mutex_lock(&qLock);
////////////// MODIFYING QUEUE/////////////////////////////////////////////////
	    // Remove an item from the queue.
	    copy(q.data + q.front, &curr);
	    q.front++;
	    q.front %= q.capacity;
	    q.size--;
	    pthread_mutex_unlock(&qLock);
////////////// DONE MODIFYING QUEUE////////////////////////////////////////////

	    if (q.size != 0) {
		printf("%u message(s) in queue.\n\n", q.size);
	    }

	    // Print information about message.
	    if (curr.inContacts) {
            	printf("%s\t(%s)\n", curr.name, curr.number);
	    }
	    else {
		printf("%s\n", curr.number);
	    }
            int index = findIndex(numToName, curr.number, hashMapSize, FALSE);
	    if (index >= 0) {
                printf("In contacts as: %s\n", numToName[index]->name);
	    }
	    else {
		printf("Not in contacts.\n");
	    }
	    printf("%s\n\n%s\n\nReply?\n", curr.time, curr.text);

	    // Get the users response.
            char c = getchar();
            while(c == '\n'){
                c = getchar();
            }
	    getchar();

	    // If user wants to respond, respond
            if (c == 'y') {
                sendMessage(outBuffer, command, curr.number, outBufferSize);
            }

	    // Clear queue
	    else if (c == 'c') {
                pthread_mutex_lock(&qLock);
		q.back = q.front;
		q.size = 0;
		pthread_mutex_unlock(&qLock);
	    }
            printf("\n\n\n");
	}

    }
}
