# Contributing to Pintos

Following conventions rules how to contribute to this project.

## Coding Convention

Basically use [GNU Coding Standards](https://en.wikipedia.org/wiki/GNU_coding_standards). Use 2-spaces indentation. Example is shown below:

```c
int
main (int argc, char *argv[])
{
  struct gizmo foo;

  fetch_gizmo (&foo, argv[1]);

 check:
  if (foo.type == MOOMIN)
    puts ("It's a moomin.");
  else if (foo.bar < GIZMO_SNUFKIN_THRESHOLD / 2
           || (strcmp (foo.class_name, "snufkin") == 0)
               && foo.bar < GIZMO_SNUFKIN_THRESHOLD)
    puts ("It's a snufkin.");
  else
    {
      char *barney;  /* Pointer to the first character after
                        the last slash in the file name.  */
      int wilma;     /* Approximate size of the universe.  */
      int fred;      /* Max value of the `bar' field.  */

      do
        {
          frobnicate (&foo, GIZMO_SNUFKIN_THRESHOLD,
                      &barney, &wilma, &fred);
          twiddle (&foo, barney, wilma + fred);
        }
      while (foo.bar >= GIZMO_SNUFKIN_THRESHOLD);

      store_size (wilma);

      goto check;
    }

  return 0;
}

```

## Commit Convention

Each commit should be done for a single functionality.

First line of a commit message should be less than or equal to 50 words with
following format which summarizes the commit:

```
[#IssueNumber] Starts with a capital letter ends without comma
```

Seperate paragraph with a single blank line. Explain details of the commit from
the second paragraph. Each line in a paragraph should be less than or equal to
80 words. Use [Github Flavored Markdown](https://guides.github.com/features/mastering-markdown/#GitHub-flavored-markdown).

## Branch Naming Convention

`master` and `develop` branches are basic. Try to use

- `iss/IssueNum`

to name branches for issues.

## Pull Request Convention

All works for an issue is done in an issue branch with naming it like above.
Say it `iss/10`. And then check coding conventions and rebase commits on
`iss/10` with `git rebase -i`. Push `iss/10` to `origin` with `git push origin
iss/10`.

In GitHub, make a PR from `iss/10` to `develop` with title
`[#IssueNum] IssueTitle` and add reviewers and extra informations for the PR.
Then, other team members will review your code.

## Review Convnetion

Every reviewers should review codes modified by PR. Reviewers can request
changes. All members should participate in reviewing and discussing as much
possible.

If an assignee should update code, use a *force push*.

After reviewing, last reviewer should merge and then delete an issue branch.
