package com.example.pm_app

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.util.*

class MainActivity : ComponentActivity() {

    // adaptorul bluetooth al telefonului,
    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }

    // Solicitare permisiuni Bluetooth la pornire
    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.values.all { it }) {
            Toast.makeText(this, "Permisiuni Bluetooth acordate!", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Aplicatia are nevoie de permisiuni pentru a se conecta la HC-05!", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Verificare si solicitare permisiuni pentru versiunile noi de Android
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                requestPermissionLauncher.launch(arrayOf(
                    Manifest.permission.BLUETOOTH_CONNECT,
                    Manifest.permission.BLUETOOTH_SCAN
                ))
            }
        }

        setContent {
            // Aplic o tema Dark Mode nativa (Fundal albastru inchis/gri)
            MaterialTheme(colorScheme = darkColorScheme(background = Color(0xFF0F172A))) {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    SmartPlantApp(bluetoothAdapter)
                }
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
fun SmartPlantApp(bluetoothAdapter: BluetoothAdapter?) {
    var isConnected by remember { mutableStateOf(false) }
    var isConnecting by remember { mutableStateOf(false) }
    var socket by remember { mutableStateOf<BluetoothSocket?>(null) }

    // Variabile de stare pentru parametrii cititi din Arduino
    var umiditate by remember { mutableIntStateOf(0) }
    var temperatura by remember { mutableIntStateOf(0) }
    var lumina by remember { mutableIntStateOf(0) }
    var pompaMerge by remember { mutableStateOf(false) }
    var ventMerge by remember { mutableStateOf(false) }


    // pentru drop-down-ul cu categorii de plante:
    var expanded by remember {mutableStateOf((false))}
    var selectedPlant by remember {mutableStateOf("Planta normala")}
    val plantOptions = listOf(
        "🌵 Cactus",
        "🌿 Ferna",
        "🌸 Normala"
    )

    val coroutineScope = rememberCoroutineScope()


    // Trimite comenzi de o singura litera ('S' sau 'E') catre Arduino
    fun sendCommand(cmd: String) {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                // Ma asigur ca trimit strict caracterul curat, fara spatii
                val cleanCmd = cmd.trim()
                socket?.outputStream?.write(cleanCmd.toByteArray(Charsets.US_ASCII))
                socket?.outputStream?.flush()
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    // functie ce asculta continuu datele transmise de Arduino in fundal (fara sa blocheze ecranul)
    fun listenToArduino(inputStream: InputStream) {
        coroutineScope.launch(Dispatchers.IO) {
            val buffer = ByteArray(1024)
            var message = ""
            while (isConnected) {
                try {
                    val bytes = inputStream.read(buffer)
                    val readMessage = String(buffer, 0, bytes)
                    message += readMessage

                    // Cand detectez sfarsitul de linie '\n' inseamna ca pachetul de la Arduino e complet
                    while (message.contains("\n")) {

                        val line = message.substringBefore("\n").trim()
                        message = message.substringAfter("\n")
                        val parts = line.split(",")
                        if (parts.size == 5) {
                            withContext(Dispatchers.Main) {
                                umiditate = parts[0].toIntOrNull() ?: 0
                                temperatura = parts[1].toIntOrNull() ?: 0
                                lumina = parts[2].toIntOrNull() ?: 0
                                pompaMerge = parts[3] == "1"
                                ventMerge = parts[4] == "1"
                            }
                        }
                    }
                } catch (e: Exception) {
                    withContext(Dispatchers.Main) {
                        isConnected = false
                    }
                    break
                }
            }
        }
    }

    // Caut HC-05 in lista telefonului si deschid conexiunea seriala
    fun connectToHC06(context: Context) {
        isConnecting = true
        coroutineScope.launch(Dispatchers.IO) {
            val pairedDevices = bluetoothAdapter?.bondedDevices
            // Filtrez dispozitivele salvate dupa numele "HC-05"
            val hc06 = pairedDevices?.find { it.name == "HC-05" }

            if (hc06 != null) {
                try {
                    val uuid = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB") // UUID-ul standard pentru comunicatie Seriala SPP
                    val tmpSocket = hc06.createRfcommSocketToServiceRecord(uuid)
                    tmpSocket.connect() // Conectare (Aici se opreste ledul de pe HC-05 din clipit)

                    withContext(Dispatchers.Main) {
                        socket = tmpSocket
                        isConnected = true
                        isConnecting = false
                    }
                    // Porneste ascultarea fluxului de date
                    listenToArduino(tmpSocket.inputStream)
                } catch (e: Exception) {
                    withContext(Dispatchers.Main) {
                        isConnecting = false
                        Toast.makeText(context, "Conexiunea a esuat. Verifica circuitul!", Toast.LENGTH_SHORT).show()
                    }
                    e.printStackTrace()
                }
            } else {
                withContext(Dispatchers.Main) {
                    isConnecting = false
                    Toast.makeText(context, "Modulul HC-05 nu este imperecheat cu telefonul!", Toast.LENGTH_LONG).show()
                }
            }
        }
    }



    // Design-ul interfetei


    val context = androidx.compose.ui.platform.LocalContext.current
    if (!isConnected) {
        // Ecranul 1 -> Bine ați venit
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text("🌱", fontSize = 90.sp)
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = "Bine ai venit la\nSmart Plant!",
                fontSize = 34.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                textAlign = TextAlign.Center,
                lineHeight = 42.sp
            )
            Spacer(modifier = Modifier.height(45.dp))

            Button(
                onClick = { if (!isConnecting) connectToHC06(context) },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFBEF264)), // Verde deschis/Lime modern
                modifier = Modifier.fillMaxWidth(0.85f).height(60.dp),
                shape = RoundedCornerShape(16.dp)
            ) {
                if (isConnecting) {
                    CircularProgressIndicator(color = Color.Black, modifier = Modifier.size(24.dp))
                } else {
                    Text("Monitorizează", fontSize = 20.sp, color = Color.Black, fontWeight = FontWeight.Bold)
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "Asigură-te că ai modulul HC-05 împerecheat (Paired) în setările Bluetooth ale telefonului tău înainte de a apăsa butonul.",
                color = Color.Gray,
                fontSize = 14.sp,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 16.dp)
            )
        }
    } else {
        // Cel de-al doilea exran: Dashboard monitorizare si control
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp)
        ) {
            Spacer(modifier = Modifier.height(24.dp))

            // Parametrii (Caseta din coltul de sus stanga)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Start
            ) {
                Box(
                    modifier = Modifier
                        .background(Color(0xFF1E293B), RoundedCornerShape(16.dp))
                        .border(1.dp, Color(0xFFBEF264).copy(alpha = 0.2f), RoundedCornerShape(16.dp))
                        .padding(20.dp)
                        .widthIn(max = 280.dp)
                ) {
                    Column {
                        Text("💧 Umiditate: $umiditate%", fontSize = 20.sp, fontWeight = FontWeight.Medium, color = Color.White)
                        Spacer(modifier = Modifier.height(10.dp))
                        Text("🌡️ Temp: $temperatura °C", fontSize = 20.sp, fontWeight = FontWeight.Medium, color = Color.White)
                        Spacer(modifier = Modifier.height(10.dp))
                        Text("☀️ Lumină: $lumina", fontSize = 20.sp, fontWeight = FontWeight.Medium, color = Color.White)
                    }
                }
            }

            Spacer(modifier = Modifier.weight(1f))

            // Starea sistemului (Mesajul din centru)
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "STAREA SISTEMULUI",
                    fontSize = 14.sp,
                    color = Color.Gray,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 2.sp
                )
                Spacer(modifier = Modifier.height(12.dp))

                // Calcul dinamic pentru mesajul text
                val stareText = when {
                    pompaMerge && ventMerge -> "Pompa și ventilatorul merg"
                    pompaMerge -> "Pompa merge"
                    ventMerge -> "Ventilatorul merge"
                    else -> "Sistem Oprit / Așteptare"
                }

                // Textul devine verde lime daca lucreaza ceva, altfel alb standard
                val stareColor = if (pompaMerge || ventMerge) Color(0xFFBEF264) else Color.White

                Text(
                    text = stareText,
                    fontSize = 28.sp,
                    fontWeight = FontWeight.Bold,
                    color = stareColor,
                    textAlign = TextAlign.Center,
                    lineHeight = 36.sp
                )
            }

            Spacer(modifier = Modifier.height(24.dp))

            Text(
                text = "Selectează planta",
                color = Color.White,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(12.dp))

            Box {
                Button(
                    onClick = { expanded = true },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF1E293B)
                    )
                ) {
                    Text(selectedPlant, color = Color.White)
                }

                DropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    plantOptions.forEach { plant ->
                        DropdownMenuItem(
                            text = { Text(plant) },
                            onClick = {
                                selectedPlant = plant
                                expanded = false
                                when (plant) {
                                    "🌵 Cactus" -> sendCommand("C")
                                    "🌿 Ferna" -> sendCommand("F")
                                    "🌸 Normala" -> sendCommand("N")
                                }
                            }
                        )
                    }
                }
            }
            Spacer(modifier = Modifier.weight(1f))
            // Butoanele de control (pozitionate jos)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Button(
                    onClick = { sendCommand("S") }, // Trimite caracterul 'S' prin UART
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFEF4444)), // Rosu de avertizare/stop
                    modifier = Modifier.weight(1f).height(65.dp),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Text("Start / Stop", fontSize = 18.sp, color = Color.White, fontWeight = FontWeight.SemiBold)
                }

                Spacer(modifier = Modifier.width(16.dp))

                Button(
                    onClick = { sendCommand("E") }, // Trimite caracterul 'E' prin UART
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFBEF264)), // Verde ECO
                    modifier = Modifier.weight(1f).height(65.dp),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Text("Start ECO", fontSize = 18.sp, color = Color.Black, fontWeight = FontWeight.Bold)
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}