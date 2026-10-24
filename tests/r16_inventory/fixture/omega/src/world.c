/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* R16 inventory fixture. while (1) { } in a comment is not a site. */
static const char *s = "run_until_complete while(1) for(;;)";
int pop(void);
int ready(void);
int usleep(unsigned);

int worker_main(void) {
    for (;;) {
        if (pop()) break;
    }
    return 0;
}

int data_loop(int n) {
    int t = 0;
    for (int i = 0; i < n; i++) t += i; /* bounded data loop: not a site */
    while (t > 100) t -= 7;               /* no wait word; "return" is not "turn" */
    return t + (s != 0);
}

int wait_ready(void) {
    for (int i = 0; i < 100; i++) {
        if (ready()) return 0;
        usleep(1000);
    }
    return -1;
}
