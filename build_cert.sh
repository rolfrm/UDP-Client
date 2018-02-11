mkdir certs
openssl req -x509 -newkey rsa:4096 -keyout certs/client-key.pem -out certs/client-cert.pem -days 365 -nodes
