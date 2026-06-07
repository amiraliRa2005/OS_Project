#include "user.h"
#include "kernel/proc_tree.h"   

void print_tree_recursive(struct proc_info *procs, int count, int pid, char *prefix, int is_last) {
  struct proc_info *current = 0;
  for(int i = 0; i < count; i++) {
    if(procs[i].pid == pid) {
      current = &procs[i];
      break;
    }
  }
  if(!current) return;

  if(prefix[0] == '\0') {
    printf("PID: %d\n", pid);
  } else if(is_last) {
    printf("%s└─ PID: %d\n", prefix, pid);
  } else {
    printf("%s├─ PID: %d\n", prefix, pid);
  }

  for(int i = 0; i < current->child_count; i++) {
    char new_prefix[128];
    int lp = 0;
    /* copy prefix into new_prefix */
    for(char *p = prefix; *p && lp < (int)sizeof(new_prefix)-1; p++){
      new_prefix[lp++] = *p;
    }

    /* append appropriate connector (avoid using strcat) */
    if(is_last) {
      /* add three spaces "   " */
      if(lp <= (int)sizeof(new_prefix)-4) {
        new_prefix[lp++] = ' ';
        new_prefix[lp++] = ' ';
        new_prefix[lp++] = ' ';
      }
    } else {
      /* add "|   " -> '|' then three spaces (match previous visual) */
      if(lp <= (int)sizeof(new_prefix)-4) {
        new_prefix[lp++] = '|';
        new_prefix[lp++] = ' ';
        new_prefix[lp++] = ' ';
        new_prefix[lp++] = ' ';
      }
    }
    /* terminate */
    new_prefix[lp] = '\0';

    print_tree_recursive(procs, count, current->children[i], new_prefix, i == current->child_count - 1);
  }
}

int main(int argc, char *argv[]) {
  struct proc_tree tree;
  int root_pid = 1;

  if(argc > 1) root_pid = atoi(argv[1]);

  
  if(ptree(root_pid, &tree) < 0) {
    printf("ptree failed\n");
    exit(1);
  }

  printf("=== Process Tree ===\n");
  print_tree_recursive(tree.procs, tree.count, root_pid, "", 1);
  exit(0);
}
