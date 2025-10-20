#pragma once
// Shim de compatibilité pour l'ancien header RGBConverter.h
// Redirige vers la lib maintenue "ColorConverter"

#include <ColorConverter.h>

// Alias de type : tout code qui utilise RGBConverter utilisera ColorConverter
using RGBConverter = ColorConverter;

