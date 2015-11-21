dtls:
	gcc dtls_udp_echo.c -lssl -lcrypto -lpthread
	openssl req -x509 -newkey rsa:2048 -keyout certs/client-key.pem -out certs/client-cert.pem -days 10 -nodes
	openssl req -x509 -newkey rsa:2048 -keyout certs/server-key.pem -out certs/server-cert.pem -days 10 -nodes

	# echo hello
	# openssl req -new > certs/client-cert.csr
	# openssl rsa -in privkey.pem -out certs/client-key.pem
	# openssl x509 -in certs/client-cert.csr -out certs/client-cert.pem -req -signkey certs/client-key.pem -days 1001
	# cat certs/client-key.pem>>certs/client-cert.pem

	# openssl req -new > certs/server-cert.csr
	# openssl rsa -in privkey.pem -out certs/server-key.pem
	# openssl x509 -in certs/server-cert.csr -out certs/server-cert.pem -req -signkey certs/server-key.pem -days 1001
	# cat certs/server-key.pem>>certs/server-cert.pem

ssl:
	gcc ssl.c dtls_udp_echo_mod.c udp.c ../../iron/mem.c ../../iron/log.c ../../iron/array.c ../../iron/math.c -lssl -lcrypto -lpthread -I../../ -Wextra -o test1.exe -O0 -g3
