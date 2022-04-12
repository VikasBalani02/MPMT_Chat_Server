server:
	gcc mpts.c -o mpts -lpthread
client:
	gcc chat_client.c -o chat_client