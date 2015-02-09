# Makefile for loader

CC := gcc
RM := /bin/rm -f

all : loader

loader: loader.o main.o spiNN_runtime.o
	$(CC) -o loader spiNN_runtime.o loader.o main.o -lpthread
	
spiNN_runtime.o: spiNN_runtime.c
	$(CC) -c spiNN_runtime.c
	
loader.o: loader.c
	$(CC) -c loader.c
	
main.o: main.c
	$(CC) -c main.c
	
clean: 
	$(RM) spiNN_runtime.o loader.o main.o loader
