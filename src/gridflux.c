/*
 * This file is part of gridflux.
 *
 * gridflux is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gridflux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gridflux.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2024 Ardi Nugraha
 */

#include "gridflux.h"
#include "xwm.h"
#include <stdlib.h>
#include <string.h>

int main() {
#ifdef __linux
  char *session_type = getenv("XDG_SESSION_TYPE");
  if (session_type != NULL) {
    if (strcmp(session_type, GF_X11) == 0) {
      LOG(GF_INFO, " X11 Session detected. \n");
      gf_x_run_layout();
    } else {
      printf("The session %s type is not supported.\n", session_type);
    }
  } else {
    printf("The session is not set.\n");
  }
#endif
  return 0;
}
