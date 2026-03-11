#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include "httplib.h"
#include "Commands/parser.h"

int main(int argc, char* argv[]) {
    srand(time(nullptr));

    // If a file argument is provided, run in CLI mode
    if (argc > 1 && std::string(argv[1]) != "--server") {
        std::cout << "========================================" << std::endl;
        std::cout << " MIA Proyecto 1 - EXT2 File System" << std::endl;
        std::cout << "========================================" << std::endl;

        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "ERROR: No se pudo abrir el archivo: " << argv[1] << std::endl;
            return 1;
        }
        std::string line;
        while (std::getline(file, line)) {
            std::string result = processLine(line);
            if (!result.empty()) {
                std::cout << result << std::endl;
            }
        }
        file.close();

        std::cout << "========================================" << std::endl;
        std::cout << " Ejecucion finalizada" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    }

    // Server mode
    httplib::Server svr;

    // Serve static files (frontend)
    svr.set_mount_point("/", "./frontend");

    // CORS headers
    svr.set_post_routing_handler([](const auto& req, auto& res) {
        (void)req;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // Handle OPTIONS preflight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // API endpoint: execute commands
    svr.Post("/api/execute", [](const httplib::Request& req, httplib::Response& res) {
        std::string script = req.body;
        std::istringstream stream(script);
        std::string line;
        std::string output;

        while (std::getline(stream, line)) {
            std::string result = processLine(line);
            if (!result.empty()) {
                output += result + "\n";
            }
        }

        res.set_content(output, "text/plain");
    });

    // API endpoint: health check
    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"running\",\"message\":\"MIA Proyecto 1 - EXT2 File System Backend\"}", "application/json");
    });

    int port = 8080;
    std::cout << "========================================" << std::endl;
    std::cout << " MIA Proyecto 1 - EXT2 File System" << std::endl;
    std::cout << " Servidor iniciado en http://localhost:" << port << std::endl;
    std::cout << " API disponible en http://localhost:" << port << "/api/execute" << std::endl;
    std::cout << "========================================" << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "ERROR: No se pudo iniciar el servidor en el puerto " << port << std::endl;
        return 1;
    }

    return 0;
}
