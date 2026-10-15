// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: procesarBoton(): eventos de entrada.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PROCESAMIENTO INTERACTIVO DE LOS EVENTOS DE ENTRADA
// ============================================================================
// Todo se puede hacer solo con el encoder: girar mueve el foco (o cambia el valor que se
// edita) y pulsar ejecuta. La larga del eje es solo un atajo. El PRG corto es "atras":
// sale de la edicion, sube un nivel y, en las pantallas principales, lleva a Inicio.
// La larga del PRG duerme la placa (deep sleep; se despierta pulsando PRG).
void dormirChispas(const char* motivo);  // Definida antes de setup()

void procesarBoton() {
  EventoBoton ev = (eventoSerie != BOTON_NINGUNO) ? eventoSerie : leerEventoBoton();
  if (eventoSerie != BOTON_NINGUNO) giroRapido = false;
  eventoSerie = BOTON_NINGUNO;
  if (ev == BOTON_NINGUNO) return;
  if (ev == BOTON_LARGA) {
    dormirChispas("PRG LARGO");  // No vuelve: al despertar, la placa arranca de nuevo
    return;
  }

  ultimaActividadMs = millis();
  if (caraActiva) {
    // Con la cara a la vista, el gesto solo la quita: se descartan los pasos de giro que queden
    portENTER_CRITICAL(&muxEncoder);
    encPasos = 0;
    portEXIT_CRITICAL(&muxEncoder);
    desactivarCara();
    return;
  }

  // Con el QR del portal a la vista, cualquier giro o pulsacion (tambien "atras") vuelve a los datos
  if (viendoQrPortal()) {
    vistaPortal = VISTA_DATOS;
    actualizarPantalla();
    return;
  }

  int8_t giro = (ev == ENC_DERECHA) ? 1 : (ev == ENC_IZQUIERDA) ? -1 : 0;
  bool pulsa = (ev == ENC_PULSA);
  bool ejeLarga = (ev == ENC_LARGA);
  bool atras = (ev == BOTON_CORTA);

  // Mientras se edita un valor, "atras" y la larga del eje solo salen de la edicion
  if ((atras || ejeLarga) && editandoValor) {
    editandoValor = false;
    actualizarPantalla();
    return;
  }

  if (pantallaActual == PANTALLA_LORA) {
    if (!enPantalla4_Sub) {
      if (atras) {
        entrarEnPantalla(PANTALLA_INICIO);
      } else if (giro != 0) {
        // Foco: [sig.] -> [PRACTICA DUO] -> [QSO LIBRE] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla4, 2, giro);
      } else if (pulsa) {
        if (opcionPantalla4 == 0) {
          enPantalla4_Sub = true; subpantalla4Activa = 1;
          modoRadioRxActivo = true; opcionDuo4_1 = 0; loraEnAjustes = false;
          loraEscuchar();  // La radio duerme fuera de Duo y QSO
        } else if (opcionPantalla4 == 1) {
          entrarEnQSO();
        } else {
          navegarDesdeBarra();
        }
      }

    } else if (loraEnAjustes) {
      // Ajustes ("menu"). Duo: 0 wpm, 1 espaciado, 2 leccion, 3 texto, 4 salir, 5 volver.
      // QSO: 0 wpm, 1 ver, 2 salir, 3 volver (las palas siguen funcionando)
      bool esDuo = (subpantalla4Activa == 1);
      uint8_t n = esDuo ? N_AJUSTES_DUO : N_AJUSTES_QSO_LORA;
      if (atras) {
        loraEnAjustes = false;
      } else if (giro != 0) {
        if (editandoValor) {
          if (!esDuo) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
          else if (opcionAjustesLora == 0) ajustarValor(wpmCW, giro, 15, 30);
          else if (opcionAjustesLora == 1) ajustarValor(espaciadoCW, giro * 10, 100, 200);
          else ajustarValor(leccionCW, giro, 1, 40);
        } else {
          opcionAjustesLora = (opcionAjustesLora + n + 1 + giro) % (n + 1);
        }
      } else if (pulsa) {
        if (opcionAjustesLora == n) loraEnAjustes = false;                       // volver
        else if (opcionAjustesLora == n - 1) salirModoLoRa();                   // salir
        else if (!esDuo && opcionAjustesLora == 1) qsoVerRx = !qsoVerRx;
        else if (esDuo && opcionAjustesLora == 3) textoDuo = (textoDuo + 1) % (TEXTO_QSO + 1);
        else editandoValor = !editandoValor;                                    // Pulsar, girar y pulsar
      }

    } else if (subpantalla4Activa == 1) {
      // Duo. Foco: 0 enviar, 1 ver (solo si hay grupo oculto), 2 menu
      if (atras) {
        salirModoLoRa();
      } else if (giro != 0) {
        do {
          opcionDuo4_1 = (opcionDuo4_1 + 3 + giro) % 3;
        } while (opcionDuo4_1 == 1 && !hayGrupoDuoPorRevelar());
      } else if (ejeLarga) {
        grupoDuoRevelado = true;  // Atajo de "ver"
        if (opcionDuo4_1 == 1) opcionDuo4_1 = 0;
      } else if (pulsa) {
        if (opcionDuo4_1 == 0) {
          String nuevoGrupo = "";
          if (textoDuo == TEXTO_INDICATIVOS) nuevoGrupo = indicativoAleatorio();
          else if (textoDuo == TEXTO_QSO) nuevoGrupo = fraseQsoAleatoria();  // Hasta 30: cabe en una trama
          else for (int i = 0; i < caracteresCW; i++) nuevoGrupo += caracterKochAleatorio(leccionCW);
          transmitirGrupoDuoLoRa(nuevoGrupo);
        } else if (opcionDuo4_1 == 1) {
          grupoDuoRevelado = true;
          opcionDuo4_1 = 0;
        } else {
          loraEnAjustes = true;
          opcionAjustesLora = 0;
        }
      }

    } else if (subpantalla4Activa == 2) {
      // QSO. Foco: 0 borrar (los dos textos), 1 menu. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirModoLoRa();
      } else if (giro != 0) {
        opcionQSO = (opcionQSO + OPCIONES_QSO + giro) % OPCIONES_QSO;
      } else if (pulsa) {
        if (opcionQSO == 0) qsoTextoRx = qsoTextoTx = "";
        else { loraEnAjustes = true; opcionAjustesLora = 0; }
      }
    }

  } else if (pantallaActual == PANTALLA_TRX) {
    switch (subTrx) {
      case TRX_MENU:
        // Foco: [sig.] -> [DECODIFICAR] -> [OPERAR] -> [QSO] -> [memorias] -> [ant.] -> [sig.] ...
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcionTrx, TRX_OPCION_BARRA, giro);
        else if (pulsa) {
          if (opcionTrx == 0) trxEntrarSub(TRX_DECOD);
          else if (opcionTrx == 1) trxEntrarSub(TRX_OPERAR);
          else if (opcionTrx == 2) trxEntrarSub(TRX_QSO);
          else if (opcionTrx == TRX_OPCION_MEMORIAS) trxEntrarSub(TRX_MEMORIAS);
          else navegarDesdeBarra();
        }
        break;

      case TRX_DECOD:
        if (decodEnAjustes) {
          // Ajustes. Foco: 0 tono (AUTO o 400-1000 Hz), 1 salir, 2 volver (pie)
          if (atras) decodEnAjustes = false;
          else if (giro != 0) {
            if (editandoValor) ajustarTonoDecod(giro);
            else opcionAjustesDecod = (opcionAjustesDecod + N_AJUSTES_DECOD + 1 + giro) % (N_AJUSTES_DECOD + 1);
          } else if (pulsa) {
            if (opcionAjustesDecod == 0) editandoValor = !editandoValor;
            else if (opcionAjustesDecod == 1) { decodEnAjustes = false; trxSalirAlMenu(); }
            else decodEnAjustes = false;
          }
        } else {
          // Foco: 0 borrar, 1 menu
          if (atras || ejeLarga) trxSalirAlMenu();
          else if (giro != 0) opcionTrxSub = (opcionTrxSub + 1) % 2;
          else if (pulsa) {
            if (opcionTrxSub == 0) trxTextoRx = "";
            else { decodEnAjustes = true; opcionAjustesDecod = 0; }
          }
        }
        break;

      case TRX_OPERAR:
      case TRX_QSO:
        // OPERAR y QSO. Foco: 0 mem, 1 tune, 2 menu (ajustes). Las palas manipulan la emisora con cualquier foco
        if (atras || ejeLarga) trxSalirAlMenu();
        else if (giro != 0) opcionTrxSub = (opcionTrxSub + 3 + giro) % 3;
        else if (pulsa) {
          if (opcionTrxSub == 0) trxAbrirListaEnvio();
          else if (opcionTrxSub == 1) trxTune();
          else {
            trxAjustesOrigen = subTrx;
            subTrx = TRX_AJUSTES;
            opcionAjustesTrx = 0;
          }
        }
        break;

      case TRX_AJUSTES: {
        // Foco: 0 wpm, 1 tono local, 2 tune (s), 3 SU, 4 salir, 5 volver (pie). Las palas siguen manipulando
        const uint8_t N_FOCO = N_AJUSTES_TRX + 1;
        if (atras) subTrx = trxAjustesOrigen;
        else if (giro != 0) {
          if (editandoValor && opcionAjustesTrx == 0) ajustarValor(trxWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
          else if (editandoValor) ajustarValor(trxTuneSeg, giro, TRX_TUNE_MIN_S, TRX_TUNE_MAX_S);
          else opcionAjustesTrx = (opcionAjustesTrx + N_FOCO + giro) % N_FOCO;
        } else if (pulsa) {
          switch (opcionAjustesTrx) {
            case 0: case 2: editandoValor = !editandoValor; break;
            case 1: trxTonoLocal = !trxTonoLocal; break;
            case 3: trxAbrirEditor(-1); break;  // Vuelve aqui
            case 4: trxSalirAlMenu(); break;
            default: subTrx = trxAjustesOrigen; break;
          }
        }
        break;
      }

      case TRX_MEMORIAS:
      case TRX_LISTA_ENVIO: {
        // Foco: memoria 1..6 o volver. Pulsar: editarla (MEMORIAS) o enviarla por KEY (lista de envio)
        bool paraEnviar = (subTrx == TRX_LISTA_ENVIO);
        bool volver = atras || (pulsa && trxMemoriaSel == TRX_NUM_MEMORIAS);
        if (volver) {
          if (paraEnviar) trxVolverDeLista();
          else subTrx = TRX_MENU;
        } else if (giro != 0) {
          trxMemoriaSel = (trxMemoriaSel + TRX_NUM_MEMORIAS + 1 + giro) % (TRX_NUM_MEMORIAS + 1);
        } else if (pulsa) {
          if (paraEnviar) trxEnviarMemoria(trxMemoriaSel);
          else trxAbrirEditor(trxMemoriaSel);
        }
        break;
      }

      case TRX_EDITOR:
        // Girar recorre la rueda; pulsar aplica (letra, ESP, {MI}, {SU}, <-, CLR, OK, ESC). PRG = ESC, larga = OK
        if (atras) trxCerrarEditor(false);
        else if (ejeLarga) trxCerrarEditor(true);
        else if (giro != 0) editorSel = (editorSel + EDITOR_TAM_RUEDA + giro) % EDITOR_TAM_RUEDA;
        else if (pulsa) editorAplicar();
        break;
    }

  } else if (pantallaActual == PANTALLA_CONFIG) {
    switch (subConf) {
      case CONF_MENU:
        // Foco: [sig.] -> [CONFIG] -> [STATUS] -> [ant.]
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcionPantallaConfig, OPCION_BARRA_CONFIG, giro);
        else if (pulsa) {
          if (opcionPantallaConfig == 0) { subConf = CONF_CONFIG; focoConf = 0; confirmandoFabrica = false; }
          else if (opcionPantallaConfig == 1) { subConf = CONF_STATUS; paginaStatus = 0; }
          else navegarDesdeBarra();
        }
        break;

      case CONF_CONFIG:
        // Lista: ID, LOCATOR, ALTITUD, HORA, RELOJ, TONO, ACELERACION, SALVAPANTALLAS, APAGADO, WIFI DATOS, WEBCONF, ACTUALIZAR FW,
        // DEPURACION, FABRICA y volver (pie)
        if (atras) {
          subConf = CONF_MENU;
          confirmandoFabrica = false;
        } else if (giro != 0) {
          if (editandoValor && focoConf == OC_ACELERACION) {
            ajustarAceleracion(giro);
            guardarAceleracionNVS();
          } else if (editandoValor && focoConf == OC_APAGADO) {
            ajustarApagado(giro);
            guardarApagadoNVS();
          } else if (editandoValor && focoConf == OC_SALVAPANTALLAS) {
            ajustarValor(caraEsperaIdx, giro, 0, N_OPCIONES_CARA - 1);
            guardarSalvapantallasNVS();
          } else if (editandoValor && focoConf == OC_LOCATOR) {
            locatorEditAuto = !locatorEditAuto;
          } else if (editandoValor && focoConf == OC_WIFI_DATOS) {
            wifiAuto = !wifiAuto;
            guardarWifiAutoNVS();
            Serial.printf("[CONFIG] WIFI DATOS %s\n", wifiAuto ? "AUTO" : "MANUAL");
          } else if (editandoValor && focoConf == OC_HORA) {
            horaUtcCabecera = !horaUtcCabecera;
            guardarEstacionNVS();
          } else if (editandoValor && focoConf == OC_ALTITUD) {
            ajustarValor(altitudEditada, giro, ALTITUD_MIN_M, ALTITUD_MAX_M);
          } else if (editandoValor && focoConf == OC_TONO) {
            ajustarValor(frecTonoHz, giro * TONO_PASO_HZ, TONO_MIN_HZ, TONO_MAX_HZ);
            aplicarFrecTono(true);  // Pitido de prueba con el tono nuevo
            guardarTonoNVS();
          } else {
            confirmandoFabrica = false;
            focoConf = (focoConf + N_OPCIONES_CONF + giro) % N_OPCIONES_CONF;
          }
        } else if (pulsa) {
          switch (focoConf) {
            case OC_ID:      confAbrirEditor(EDITOR_ID_ESTACION); break;
            case OC_LOCATOR:
              // Con GPS: pulsar, girar entre AUTO (GPS) y MANUAL y pulsar (MANUAL abre el editor). Sin GPS, el editor
              if (editandoValor) {
                editandoValor = false;
                locatorAuto = locatorEditAuto;
                guardarEstacionNVS();
                Serial.printf("[CONFIG] LOCATOR %s\n", locatorAuto ? "AUTO (GPS)" : "MANUAL");
                if (!locatorAuto) confAbrirEditor(EDITOR_LOCATOR);
                else if (gpsDespierto()) gpsVentanaHecha = false;  // Que lo ponga al cerrar esta ventana
                else if (estadoGps == GPS_DORMIDO) despertarGps(GPS_TOPE_PERIODICO_MS, "LOCATOR AUTO");
              } else if (gpsDetectado()) {
                locatorEditAuto = locatorAuto;
                editandoValor = true;
              } else {
                confAbrirEditor(EDITOR_LOCATOR);
              }
              break;
            case OC_ALTITUD:
              // Pulsar, girar hasta la altitud conocida y pulsar: el QNH sale de la presion actual
              if (editandoValor) {
                editandoValor = false;
                if (!isnan(bmePresionHpa)) {
                  qnhHpa = qnhParaAltitud(altitudEditada);
                  qnhCalibrado = true;
                  guardarQnhNVS();
                  Serial.printf("[BME] ALTITUD %d M: QNH %.1f HPA\n", altitudEditada, qnhHpa);
                }
              } else if (bmeDireccion != 0 && !isnan(bmePresionHpa)) {
                altitudEditada = (int16_t)constrain(lroundf(altitudBarometrica()), ALTITUD_MIN_M, ALTITUD_MAX_M);
                editandoValor = true;
              }
              break;
            case OC_RELOJ:
              confAbrirReloj();
              break;
            case OC_HORA:
            case OC_WIFI_DATOS:
            case OC_ACELERACION:
            case OC_SALVAPANTALLAS:
            case OC_APAGADO:
              editandoValor = !editandoValor;  // Pulsar, girar y pulsar
              break;
            case OC_TONO:
              editandoValor = !editandoValor;
              if (editandoValor) aplicarFrecTono(true);  // Al empezar a editar, se oye el tono actual
              break;
            case OC_WEBCONF:
              if (estadoWifi != WIFI_PORTAL) abrirPortalWifi();
              subConf = CONF_PORTAL;
              focoPortalConf = 0;
              break;
            case OC_ACTUALIZAR:
              confEntrarOta();
              break;
            case OC_DEPURACION:
              subConf = CONF_DEPURACION;
              focoDepu = 0;
              break;
            case OC_FABRICA:
              if (!confirmandoFabrica) confirmandoFabrica = true;
              else resetFabrica();
              break;
            default:
              subConf = CONF_MENU;
              break;
          }
        }
        break;

      case CONF_PORTAL:
        // Foco: 0 ver qr, 1 volver (sin portal abierto, solo volver). El portal sigue abierto al volver
        if (procesarVistaPortal(focoPortalConf, giro, pulsa, atras)) subConf = CONF_CONFIG;
        break;

      case CONF_DEPURACION:
        // Foco: una opcion por bit DEPU_* (pulsar cambia SI/NO) y volver (pie)
        if (atras) {
          subConf = CONF_CONFIG;
        } else if (giro != 0) {
          focoDepu = (focoDepu + N_OPCIONES_DEPU + 1 + giro) % (N_OPCIONES_DEPU + 1);
        } else if (pulsa) {
          if (focoDepu < N_OPCIONES_DEPU) {
            depuracion ^= BITS_DEPU[focoDepu];
            guardarDepuracionNVS();
            Serial.printf("[DEPURACION] %s: %s\n", NOMBRES_OPCIONES_DEPU[focoDepu],
                          depuActiva(BITS_DEPU[focoDepu]) ? "SI" : "NO");
          } else {
            subConf = CONF_CONFIG;
          }
        }
        break;

      case CONF_RELOJ: {
        // Girar cambia el campo (da la vuelta); pulsar pasa al siguiente y, en el ultimo, pone la hora. La larga del
        // eje la pone ya; atras sale sin cambiarla
        if (atras) {
          subConf = CONF_CONFIG;
        } else if (giro != 0) {
          int16_t minimo = RELOJ_MIN[relojCampo], rango = RELOJ_MAX[relojCampo] - minimo + 1;
          relojCampos[relojCampo] = (relojCampos[relojCampo] - minimo + giro % rango + rango) % rango + minimo;
        } else if (pulsa && relojCampo < RELOJ_N_CAMPOS - 1) {
          relojCampo++;
        } else if (pulsa || ejeLarga) {
          confAplicarReloj();
        }
        break;
      }

      case CONF_STATUS:
        // Girar pasa pagina; pulsar (o atras) vuelve al menu
        if (atras || pulsa || ejeLarga) subConf = CONF_MENU;
        else if (giro != 0) paginaStatus = (paginaStatus + N_PAGINAS_STATUS + giro) % N_PAGINAS_STATUS;
        break;

      case CONF_OTA:
        // Con version nueva: 0 instalar, 1 volver. Si no, cualquier pulsacion (o atras) vuelve
        if (estadoOta == OTA_DESCARGANDO || estadoOta == OTA_HECHO) break;
        if (estadoOta == OTA_HAY_NUEVA && giro != 0) focoOta = (focoOta + 1) % 2;
        else if (atras || (pulsa && (estadoOta != OTA_HAY_NUEVA || focoOta == 1))) confSalirOta();
        else if (pulsa) instalarActualizacion();
        break;

      case CONF_EDITOR:
        // Como el de las memorias: girar recorre la rueda; pulsar aplica. PRG = ESC, larga = OK
        if (atras) confCerrarEditor(false);
        else if (ejeLarga) confCerrarEditor(true);
        else if (giro != 0) { editorError = false; editorSel = (editorSel + EDITOR_TAM_RUEDA + giro) % EDITOR_TAM_RUEDA; }
        else if (pulsa) editorAplicar();
        break;
    }

  } else if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
             pantallaActual == PANTALLA_SOL) {
    // Menu: [sig.] -> [VER DATOS] -> [ACTUALIZAR] -> [ant.] (sin conexion: ABRIR WEBCONF y REINTENTAR).
    // POTA tiene ademas VER PARQUES en medio, con o sin conexion (la lista esta en la flash).
    // Paginas: girar pasa pagina y pulsar (o atras) vuelve al menu
    bool esProp = (pantallaActual == PANTALLA_PROPAGACION);
    bool esSol = (pantallaActual == PANTALLA_SOL);
    bool esPota = !esProp && !esSol;
    uint8_t& opcion = esProp ? opcionPantallaProp : (esSol ? opcionPantallaSol : opcionPantallaSpots);
    uint8_t barra = esPota ? OPCION_BARRA_SPOTS : OPCION_BARRA_DATOS;
    uint8_t& pagina = esProp ? paginaPropagacion : (esSol ? paginaSol : paginaSpots);
    uint8_t paginas = esProp ? 2 : (esSol ? paginasSol() : paginasSpots());
    if (avisoWifiVisible()) {
      // Conectando o esperando la hora tras ACTUALIZAR: atras o pulsar cancela (la descarga en si no se ve: bloquea)
      if (atras || pulsa) terminarPeticionWifi();
    } else switch (subDatos) {
      case DATOS_MENU:
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcion, barra, giro);
        else if (pulsa) {
          // En POTA, la opcion 2 (ACTUALIZAR o REINTENTAR) hace lo que la 1 en PROP y SOL
          uint8_t op = (esPota && opcion == 2) ? 1 : opcion;
          if (opcion == barra) {
            navegarDesdeBarra();
          } else if (esPota && opcion == 1) {
            buscarParquesCercanos();
            subDatos = DATOS_PARQUES;
          } else if (estadoWifi == WIFI_SIN_CONEXION) {
            if (op == 0) {
              abrirPortalWifi();
              subDatos = DATOS_PORTAL;
              focoPortalDatos = 0;
              // Al conectar desde el portal: en PROP y POTA se descarga y en SOL se espera la hora; en MANUAL,
              // luego se apaga
              wifiPorPeticion = true;
              forzarActualizacion = !esSol;
            } else if (esSol) {
              actualizarHoraSol();  // Reintentar
            } else {
              pedirActualizacionWifi();
            }
          } else if (op == 0) {
            subDatos = DATOS_PAGINAS;
            pagina = 0;
          } else if (esSol) {
            actualizarHoraSol();
          } else {
            pedirActualizacionWifi();
          }
        }
        break;

      case DATOS_PAGINAS:
        if (atras || pulsa || ejeLarga) subDatos = DATOS_MENU;
        else if (giro != 0) pagina = (pagina + paginas + giro) % paginas;
        break;

      case DATOS_PARQUES:
        if (atras || pulsa || ejeLarga) subDatos = DATOS_MENU;
        else if (giro != 0) paginaParques = (paginaParques + paginasParques() + giro) % paginasParques();
        break;

      case DATOS_PORTAL:
        if (procesarVistaPortal(focoPortalDatos, giro, pulsa, atras)) subDatos = DATOS_MENU;
        break;
    }

  } else if (pantallaActual == PANTALLA_CW) {
    if (enSim && estadoSim == SIM_AJUSTES) {
      // Ajustes del simulador. Foco: 0 wpm activador, 1 espaciado, 2 wpm palas, 3 ver, 4 iniciar, 5 volver
      if (atras) {
        salirDeSim();
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionSim) {
            case 0: ajustarValor(simWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX); break;
            case 1: ajustarValor(simEspaciado, giro * 10, 100, 200); break;
            case 2: ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX); break;
          }
        } else {
          opcionSim = (opcionSim + N_OPCIONES_SIM + giro) % N_OPCIONES_SIM;
        }
      } else if (pulsa) {
        switch (opcionSim) {
          case 3: simVerTexto = !simVerTexto; break;
          case 4: iniciarSim(); break;
          case 5: salirDeSim(); break;
          default: editandoValor = !editandoValor; break;  // 0, 1 y 2
        }
      }

    } else if (enSim && estadoSim == SIM_FIN) {
      // Resultado. Foco: 0 volver (a los ajustes), 1 otro QSO. La larga del eje es atajo de otro
      if (atras) simAAjustes();
      else if (ejeLarga) iniciarSim();
      else if (giro != 0) opcionPracticaSim = (opcionPracticaSim + 1) % 2;
      else if (pulsa) {
        if (opcionPracticaSim == 0) simAAjustes();
        else iniciarSim();
      }

    } else if (enSim) {
      // QSO. Foco: 0 repetir (lo ultimo del activador), 1 salir. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        simAAjustes();
      } else if (giro != 0) {
        opcionPracticaSim = (opcionPracticaSim + 1) % 2;
      } else if (pulsa) {
        if (opcionPracticaSim == 1) simAAjustes();
        else simRepetir();
      }

    } else if (enEcho && enEstadisticasEcho) {
      // Como ESTAD. del Koch: girar pasa pagina (la ultima es la de borrado); pulsar vuelve o borra
      if (atras || ejeLarga) {
        enEstadisticasEcho = false; confirmandoBorrado = false;
      } else if (giro != 0) {
        confirmandoBorrado = false;
        paginaStatsEcho = (paginaStatsEcho + TOTAL_PAGINAS_STATS_ECHO + giro) % TOTAL_PAGINAS_STATS_ECHO;
      } else if (pulsa) {
        if (paginaStatsEcho != TOTAL_PAGINAS_STATS_ECHO - 1) {
          enEstadisticasEcho = false;
        } else if (!confirmandoBorrado) {
          confirmandoBorrado = true;
        } else {
          borrarEstadisticasEchoNVS(); confirmandoBorrado = false; paginaStatsEcho = 0;
        }
      }

    } else if (enEcho && !echoPracticando) {
      // Ajustes. Foco: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 refuerzo, 7 estad., 8 iniciar, 9 volver
      if (atras) {
        salirDeEcho();
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionEcho) {
            case 0: ajustarValor(echoWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX); break;
            case 1: ajustarValor(echoEspaciado, giro * 10, 100, 200); break;
            case 2: ajustarValor(echoLeccion, giro, 1, 40); break;
            case 3: ajustarValor(echoLargo, giro, 1, 5); break;
            case 5: ajustarValor(echoRetos, giro * 10, 10, 50); break;
          }
        } else {
          opcionEcho = (opcionEcho + N_OPCIONES_ECHO + giro) % N_OPCIONES_ECHO;
        }
      } else if (pulsa) {
        switch (opcionEcho) {
          case 4: echoVerObjetivo = !echoVerObjetivo; break;
          case 6: echoRefuerzo = siguienteRefuerzo(echoRefuerzo); break;
          case 7:
            enEstadisticasEcho = true;  // Al salir se vuelve aqui
            paginaStatsEcho = 0;
            confirmandoBorrado = false;
            break;
          case 8: iniciarPracticaEcho(); break;
          case 9: salirDeEcho(); break;
          default: editandoValor = !editandoValor; break;  // 0, 1, 2, 3 y 5
        }
      }

    } else if (enEcho && estadoEcho == ECHO_FIN) {
      // Resultado. Foco: 0 volver (a los ajustes), 1 repetir. La larga del eje es atajo de repetir
      if (atras) salirPracticaEcho();
      else if (ejeLarga) iniciarPracticaEcho();
      else if (giro != 0) opcionPracticaEcho = (opcionPracticaEcho + 1) % 2;
      else if (pulsa) {
        if (opcionPracticaEcho == 0) salirPracticaEcho();
        else iniciarPracticaEcho();
      }

    } else if (enEcho) {
      // Practica. Foco: 0 repetir (no cuenta como intento), 1 salir. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirPracticaEcho();
      } else if (giro != 0) {
        opcionPracticaEcho = (opcionPracticaEcho + 1) % 2;
      } else if (pulsa) {
        if (opcionPracticaEcho == 1) salirPracticaEcho();
        else if (estadoEcho != ECHO_RESULTADO) echoNuevoReto(true);  // Con el resultado a la vista, sigue solo
      }

    } else if (enKeyer && keyerEnRitmo) {
      // Paginas de RITMO: girar pasa pagina; pulsar o atras vuelve al menu
      if (atras || pulsa || ejeLarga) keyerEnRitmo = false;
      else if (giro != 0) paginaRitmo = (paginaRitmo + PAGINAS_RITMO + giro) % PAGINAS_RITMO;

    } else if (enKeyer && keyerEnAjustes) {
      // Ajustes de TX LIBRE. Foco: 0 wpm, 1 modo, 2 palas, 3 ritmo, 4 salir, 5 volver (pie). Las palas siguen
      // manipulando
      const uint8_t N_FOCO = N_AJUSTES_KEYER + 1;
      if (atras) {
        keyerEnAjustes = false;
      } else if (giro != 0) {
        if (editandoValor) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
        else opcionAjustesKeyer = (opcionAjustesKeyer + N_FOCO + giro) % N_FOCO;
      } else if (pulsa) {
        switch (opcionAjustesKeyer) {
          case 0: editandoValor = !editandoValor; break;
          case 1: keyerModoB = !keyerModoB; break;
          case 2: keyerPalasInvertidas = !keyerPalasInvertidas; break;
          case 3: keyerEnRitmo = true; paginaRitmo = 0; break;
          case 4: keyerEnAjustes = false; salirDeKeyer(); break;
          default: keyerEnAjustes = false; break;
        }
      }

    } else if (enKeyer) {
      // Foco: 0 borrar, 1 menu (ajustes). Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirDeKeyer();
      } else if (giro != 0) {
        opcionKeyer = (opcionKeyer + 1) % 2;
      } else if (pulsa) {
        if (opcionKeyer == 0) { textoKeyer = ""; ritmoBorrar = true; }  // Borra tambien lo medido en RITMO
        else { keyerEnAjustes = true; opcionAjustesKeyer = 0; }
      }

    } else if (enEstadisticas3_5) {
      // Girar pasa pagina (la ultima es la de borrado). Pulsar: volver, o borrar en la ultima
      if (atras || ejeLarga) {
        enEstadisticas3_5 = false; confirmandoBorrado = false;
      } else if (giro != 0) {
        confirmandoBorrado = false;
        paginaStats3_5 = (paginaStats3_5 + TOTAL_PAGINAS_STATS + giro) % TOTAL_PAGINAS_STATS;
      } else if (pulsa) {
        if (paginaStats3_5 != PAG_STATS_BORRADO) {
          enEstadisticas3_5 = false;
        } else if (!confirmandoBorrado) {
          confirmandoBorrado = true;
        } else {
          borrarEstadisticasKochNVS(); confirmandoBorrado = false; paginaStats3_5 = 0;
        }
      }

    } else if (enPractica3_2) {
      if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
        int charPorPagina = 63;
        bool hayMasPaginas = ((paginaListado3_3 + 1) * charPorPagina) < textoCompletoLeccion.length();
        if (atras) {
          if (paginaListado3_3 > 0) { paginaListado3_3--; opcionFinListado3_3 = 0; }
        } else if (giro > 0) {
          // Derecha: pagina siguiente y, en la ultima, recorre "x letra", "total" y "rep."
          if (hayMasPaginas) paginaListado3_3++;
          else opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 3;
        } else if (giro < 0) {
          if (!hayMasPaginas && opcionFinListado3_3 > 0) opcionFinListado3_3--;
          else if (paginaListado3_3 > 0) paginaListado3_3--;
        } else if (pulsa) {
          if (hayMasPaginas) paginaListado3_3++;
          else if (opcionFinListado3_3 == 0) {
            estadoCW = CW_PANTALLA_CORREGIR_ERRORES;  // Las marcas se conservan si se vuelve al listado
            focoCorregir = 0;
            paginaCorregir = 0;
          } else if (opcionFinListado3_3 == 1) {
            // Empieza en lo marcado en "x letra" (0 si no se ha marcado nada) y ya editando
            estadoCW = CW_PANTALLA_FALLOS_TOTALES;
            fallosTotales = numeroErrores;
            focoFallosTotales = 0;
            editandoValor = true;
          }
          else iniciarPracticaCW();
        }

      } else if (estadoCW == CW_PANTALLA_FALLOS_TOTALES) {
        // Foco: 0 numero de fallos (pulsar lo edita; al girar rapido va de 5 en 5), 1 rep., 2 ok
        if (atras) {
          estadoCW = CW_PANTALLA_3_3_LISTADO;  // Si se estaba editando, "atras" ya ha salido de la edicion arriba
        } else if (giro != 0) {
          if (editandoValor) ajustarValor(fallosTotales, giro, 0, totalCaracteresLeccion);
          else focoFallosTotales = (focoFallosTotales + 3 + giro) % 3;
        } else if (pulsa) {
          if (focoFallosTotales == 0) editandoValor = !editandoValor;
          else if (focoFallosTotales == 1) { editandoValor = false; iniciarPracticaCW(); }
          else { editandoValor = false; enviarResultadoKoch(false); }
        }

      } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
        // Foco en anillo: caracteres (sin los espacios) -> rep. -> ok -> primer caracter
        uint16_t largo = textoCompletoLeccion.length();
        if (atras) {
          estadoCW = CW_PANTALLA_3_3_LISTADO;
        } else if (giro != 0) {
          int8_t paso = giro > 0 ? 1 : -1;
          for (int8_t n = abs(giro); n > 0; n--) {
            do {
              focoCorregir = (focoCorregir + largo + 2 + paso) % (largo + 2);
            } while (focoCorregir < largo && textoCompletoLeccion[focoCorregir] == ' ');
          }
          if (focoCorregir < largo) paginaCorregir = focoCorregir / CARACTERES_POR_PAGINA_LISTADO;
        } else if (pulsa) {
          if (focoCorregir < largo) {
            marcasFallo[focoCorregir] = !marcasFallo[focoCorregir];
            numeroErrores += marcasFallo[focoCorregir] ? 1 : -1;
          } else if (focoCorregir == largo) {
            iniciarPracticaCW();
          } else {
            enviarResultadoKoch(true);
          }
        }

      } else if (estadoCW == CW_PANTALLA_RESUMEN_FINAL) {
        // Foco: 0 volver, 1 repetir. La larga del eje es atajo de repetir
        if (atras) detenerPracticaCW();
        else if (ejeLarga) iniciarPracticaCW();
        else if (giro != 0) opcionResumen = (opcionResumen + 1) % 2;
        else if (pulsa) {
          if (opcionResumen == 0) detenerPracticaCW();
          else iniciarPracticaCW();
        }

      } else {
        // Practica en marcha o en pausa. Foco: 0 pausa/seguir, 1 salir. La larga del eje es atajo de salir
        if (atras || ejeLarga) {
          detenerPracticaCW();
        } else if (giro != 0) {
          opcionPractica = (opcionPractica + 1) % 2;
        } else if (pulsa) {
          if (opcionPractica == 1) detenerPracticaCW();
          else if (estadoCW == CW_PAUSADO) estadoCW = CW_GENERAR_GRUPO;
          else { estadoCW = CW_PAUSADO; motorCWDetener(); }
        }
      }

    } else if (enSubpantallaCW) {
      // Ajustes del KOCH (lista). Foco: 0 wpm, 1 espaciado, 2 leccion, 3 caracteres, 4 grupos, 5 texto,
      // 6 refuerzo, 7 estadisticas, 8 iniciar, 9 volver. Leccion, caracteres y refuerzo solo con texto GRUPOS
      const uint8_t N_FOCO = N_AJUSTES_KOCH + 2;
      if (atras) {
        enSubpantallaCW = false;
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionSubpantalla) {
            case 0: ajustarValor(wpmCW, giro, 15, 30); break;
            case 1: ajustarValor(espaciadoCW, giro * 10, 100, 200); break;
            case 2: ajustarValor(leccionCW, giro, 1, 40); break;
            case 3: ajustarValor(caracteresCW, giro, 1, 5); break;
            case 4: ajustarValor(gruposCW, giro * 10, 10, 50); break;
          }
        } else {
          opcionSubpantalla = (opcionSubpantalla + N_FOCO + giro) % N_FOCO;
        }
      } else if (pulsa) {
        bool grupos = textoKoch == TEXTO_GRUPOS;
        if (opcionSubpantalla <= 4) {
          if (grupos || (opcionSubpantalla != 2 && opcionSubpantalla != 3)) editandoValor = !editandoValor;
        }
        else if (opcionSubpantalla == 5) textoKoch = (textoKoch + 1) % (TEXTO_QSO + 1);
        else if (opcionSubpantalla == 6) { if (grupos) refuerzoKoch = siguienteRefuerzo(refuerzoKoch); }
        else if (opcionSubpantalla == 7) {
          enEstadisticas3_5 = true;  // Al salir se vuelve aqui
          paginaStats3_5 = 0;
          confirmandoBorrado = false;
        }
        else if (opcionSubpantalla == 8) iniciarPracticaCW();
        else enSubpantallaCW = false;
      }

    } else {
      if (atras) {
        entrarEnPantalla(PANTALLA_INICIO);
      } else if (giro != 0) {
        // Foco: [sig.] -> [KOCH (RX)] -> [ECHO (TX)] -> [TX LIBRE] -> [qso sim] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla3, OPCION_BARRA_CW, giro);
      } else if (pulsa) {
        if (opcionPantalla3 == 0) {
          enSubpantallaCW = true;
          opcionSubpantalla = 8;  // iniciar
        } else if (opcionPantalla3 == 1) {
          entrarEnEcho();
        } else if (opcionPantalla3 == 2) {
          entrarEnKeyer();
        } else if (opcionPantalla3 == OPCION_SIM_CW) {
          entrarEnSim();
        } else {
          navegarDesdeBarra();
        }
      }
    }

  } else {
    // Pantallas sin opciones (Inicio, Sol): el foco solo alterna entre "ant." y "sig."
    if (atras) entrarEnPantalla(PANTALLA_INICIO);
    else if (giro != 0) opcionNavegacionP1234 = (opcionNavegacionP1234 == 0) ? 1 : 0;
    else if (pulsa) navegarDesdeBarra();
  }

  actualizarPantalla();
}

