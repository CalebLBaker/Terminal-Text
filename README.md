# Terminal-Text
App for texting from the terminal of the termux app for Android.

The primary purpose of this app is for texting remotely from an Android phone that is acting as an ssh server.
This allows you to ssh into the phone from a computer and write texts using a full-sized keyboard.

After following the instructions in installation.txt, run by typing the following command:
  ./text [contactFilename]
Where [contactFilename] is the name of a file specifying contacts. If no filename is specified, contacts.txt is used.


Incoming text messages will be put in a queue and presented to you in the order they came in.
You can respond to a message in 1 of 3 ways.

  y         message should be replaced with the text of a message that will be sent back in response to the displayed message.
  message 
  
  c         Empties out the message queue.
  
  *         * can be replaced with any character except y and c. Ignores the message and goes on to the next one if there is any.


When the queue is empty you will be able to issue any of several commands:

  t recipient   recipient should be replaced with either the contact name or phone number of someone you want to text.
  message       message should be replaced with the text of a message to send to recipient.
  
  a name        name should be replaced with the name of someone to be added to your contacts. 
  number        number should be replaced with the phone number of that person.
  
  p             Prints all of your contacts to the screen.
  
  i n           n should be replaced by the number of texts from you inbox to display.
  
  b             Prints the status of your phone's battery.
  
  w             Saves your contacts to your contact file.

  q             Closes the program.
  
  x             Saves your contacts and closes the program.
  
