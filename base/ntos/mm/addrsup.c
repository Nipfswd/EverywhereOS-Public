/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    addrsup.c

Abstract:

    Virtual address descriptor tree support routines.

    The VAD tree is an AVL tree keyed on virtual page number (VPN).
    Each node is an MMVAD structure with LeftChild/RightChild/Parent
    pointers and a Balance field (-1, 0, or +1 following the convention
    left-heavy = -1, balanced = 0, right-heavy = +1).

    Exported routines:

        MiLocateAddressInTree     - Find the VAD enclosing a VPN.
        MiInsertNode              - Insert a new VAD, rebalancing as needed.
        MiRemoveNode              - Remove a VAD, rebalancing as needed.
        MiCheckForConflictingNode - Return any VAD that overlaps a range.
        MiFindEmptyAddressRangeInTree - Find a free address window of a
                                       given size.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.  Caller must hold appropriate Mm locks.

--*/

#include "../inc/mm.h"
#include "mi.h"

/* -----------------------------------------------------------------------
 * Internal AVL rotation helpers
 * ----------------------------------------------------------------------- */

//
// MiRotateRight
//
// Performs a right rotation around Node.  Parent pointers are updated.
// Returns the new root of the rotated sub-tree.
//
static PMMVAD
MiRotateRight(
    PMMVAD  Node,
    PMMVAD *RootPtr
    )
{
    PMMVAD Left = Node->LeftChild;
    PMMVAD Parent = Node->Parent;

    Node->LeftChild  = Left->RightChild;
    if (Left->RightChild != NULL) {
        Left->RightChild->Parent = Node;
    }

    Left->RightChild = Node;
    Left->Parent     = Parent;
    Node->Parent     = Left;

    if (Parent == NULL) {
        *RootPtr = Left;
    } else if (Parent->LeftChild == Node) {
        Parent->LeftChild = Left;
    } else {
        Parent->RightChild = Left;
    }

    return Left;
}

//
// MiRotateLeft
//
// Performs a left rotation around Node.  Returns the new sub-tree root.
//
static PMMVAD
MiRotateLeft(
    PMMVAD  Node,
    PMMVAD *RootPtr
    )
{
    PMMVAD Right = Node->RightChild;
    PMMVAD Parent = Node->Parent;

    Node->RightChild = Right->LeftChild;
    if (Right->LeftChild != NULL) {
        Right->LeftChild->Parent = Node;
    }

    Right->LeftChild = Node;
    Right->Parent    = Parent;
    Node->Parent     = Right;

    if (Parent == NULL) {
        *RootPtr = Right;
    } else if (Parent->LeftChild == Node) {
        Parent->LeftChild = Right;
    } else {
        Parent->RightChild = Right;
    }

    return Right;
}

/* -----------------------------------------------------------------------
 * MiLocateAddressInTree
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Searches the AVL tree rooted at Root for the VAD that contains Vpn.
    A VAD contains Vpn when Vpn >= Node->StartingVpn and
    Vpn <= Node->EndingVpn.

Arguments:

    Vpn  - Virtual page number to locate.

    Root - Root of the VAD tree to search.

Return Value:

    Pointer to the enclosing MMVAD, or NULL if no such node exists.

--*/
PMMVAD
MiLocateAddressInTree(
    ULONG_PTR Vpn,
    PMMVAD    Root
    )
{
    PMMVAD Node;

    Node = Root;

    while (Node != NULL) {

        if (Vpn < Node->StartingVpn) {
            Node = Node->LeftChild;
        } else if (Vpn > Node->EndingVpn) {
            Node = Node->RightChild;
        } else {
            return Node;
        }
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 * MiCheckForConflictingNode
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Returns the first VAD node that overlaps [StartVpn, EndVpn].
    Two ranges [a,b] and [c,d] overlap when a <= d and c <= b.

Arguments:

    StartVpn - First page of the range to check.

    EndVpn   - Last page of the range to check.

    Root     - Root of the VAD tree to search.

Return Value:

    Pointer to a conflicting MMVAD, or NULL if the range is free.

--*/
PMMVAD
MiCheckForConflictingNode(
    ULONG_PTR StartVpn,
    ULONG_PTR EndVpn,
    PMMVAD    Root
    )
{
    PMMVAD Node;

    Node = Root;

    while (Node != NULL) {

        if (EndVpn < Node->StartingVpn) {
            Node = Node->LeftChild;
        } else if (StartVpn > Node->EndingVpn) {
            Node = Node->RightChild;
        } else {
            return Node;
        }
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 * MiFindEmptyAddressRangeInTree
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Walks the VAD tree in an in-order traversal to find a gap in the
    virtual address space large enough to satisfy the request.

    The search respects Alignment (must be a power of two, in pages).
    The gap is found by tracking the "previous ending VPN" as the tree is
    walked left-to-right and looking for a hole >= SizeOfRange pages.

    The search region is bounded between MM_LOWEST_USER_ADDRESS and
    MM_HIGHEST_USER_ADDRESS.

Arguments:

    SizeOfRange - Required size in bytes.

    Alignment   - Required alignment in bytes.  Must be a power of two.

    Root        - Root of the VAD tree.

    Base        - On success, receives the virtual address of the start
                  of the free region.

Return Value:

    STATUS_SUCCESS    - A suitable region was found; *Base is valid.
    STATUS_NO_MEMORY  - No gap large enough was found.

--*/
NTSTATUS
MiFindEmptyAddressRangeInTree(
    SIZE_T     SizeOfRange,
    ULONG_PTR  Alignment,
    PMMVAD     Root,
    PULONG_PTR Base
    )
{
    PMMVAD    Node;
    PMMVAD    Stack[64];
    LONG      StackTop;
    ULONG_PTR PrevEnd;
    ULONG_PTR CandidateStart;
    ULONG_PTR SizeInPages;
    ULONG_PTR AlignPages;
    ULONG_PTR UserTopVpn;
    ULONG_PTR UserBottomVpn;

    if (SizeOfRange == 0 || Root == NULL) {
        if (SizeOfRange == 0) {
            *Base = (ULONG_PTR)MM_LOWEST_USER_ADDRESS;
            return STATUS_SUCCESS;
        }
        *Base = (ULONG_PTR)MM_LOWEST_USER_ADDRESS;
        return STATUS_SUCCESS;
    }

    SizeInPages    = BYTES_TO_PAGES(SizeOfRange);
    AlignPages     = Alignment >> PAGE_SHIFT;
    if (AlignPages == 0) {
        AlignPages = 1;
    }

    UserBottomVpn = MI_VA_TO_VPN(MM_LOWEST_USER_ADDRESS);
    UserTopVpn    = MI_VA_TO_VPN(MM_HIGHEST_USER_ADDRESS);
    PrevEnd       = UserBottomVpn - 1;
    StackTop      = -1;
    Node          = Root;

    //
    // In-order traversal via explicit stack.
    //
    for (;;) {

        while (Node != NULL) {
            if (StackTop < 63) {
                StackTop++;
                Stack[StackTop] = Node;
            }
            Node = Node->LeftChild;
        }

        if (StackTop < 0) {
            break;
        }

        Node = Stack[StackTop--];

        //
        // The gap before this node runs from (PrevEnd + 1) to
        // (Node->StartingVpn - 1).
        //
        if (Node->StartingVpn > PrevEnd + 1) {

            //
            // Align CandidateStart up.
            //
            CandidateStart = PrevEnd + 1;
            CandidateStart = (CandidateStart + AlignPages - 1) &
                             ~(AlignPages - 1);

            if (CandidateStart >= UserBottomVpn &&
                CandidateStart + SizeInPages - 1 <= Node->StartingVpn - 1 &&
                CandidateStart + SizeInPages - 1 <= UserTopVpn) {

                *Base = (ULONG_PTR)MI_VPN_TO_VA(CandidateStart);
                return STATUS_SUCCESS;
            }
        }

        PrevEnd = Node->EndingVpn;
        Node    = Node->RightChild;
    }

    //
    // Check the gap after the last VAD in the tree.
    //
    CandidateStart = PrevEnd + 1;
    CandidateStart = (CandidateStart + AlignPages - 1) & ~(AlignPages - 1);

    if (CandidateStart >= UserBottomVpn &&
        CandidateStart + SizeInPages - 1 <= UserTopVpn) {

        *Base = (ULONG_PTR)MI_VPN_TO_VA(CandidateStart);
        return STATUS_SUCCESS;
    }

    return STATUS_NO_MEMORY;
}

/* -----------------------------------------------------------------------
 * MiInsertNode
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Inserts Node into the AVL tree whose root is at *Root.  After binary
    insertion the path from the new node to the root is walked to update
    Balance fields; rotations are performed to maintain the AVL invariant.

Arguments:

    Node    - The VAD to insert.  StartingVpn and EndingVpn must be set.
              LeftChild, RightChild, and Parent are initialised here.

    Root    - Pointer to the root pointer of the tree.

Return Value:

    None.

--*/
VOID
MiInsertNode(
    PMMVAD  Node,
    PMMVAD *Root
    )
{
    PMMVAD  Current;
    PMMVAD  Parent;
    PMMVAD  NewSubRoot;

    Node->LeftChild  = NULL;
    Node->RightChild = NULL;
    Node->Balance    = 0;

    if (*Root == NULL) {
        Node->Parent = NULL;
        *Root = Node;
        return;
    }

    //
    // Standard BST insertion.
    //
    Current = *Root;
    Parent  = NULL;

    while (Current != NULL) {
        Parent = Current;
        if (Node->StartingVpn < Current->StartingVpn) {
            Current = Current->LeftChild;
        } else {
            Current = Current->RightChild;
        }
    }

    Node->Parent = Parent;

    if (Node->StartingVpn < Parent->StartingVpn) {
        Parent->LeftChild = Node;
    } else {
        Parent->RightChild = Node;
    }

    //
    // Walk upward and update balance factors, rotating when necessary.
    //
    Current = Node;

    while (Current->Parent != NULL) {
        Parent = Current->Parent;

        if (Parent->LeftChild == Current) {
            Parent->Balance--;
        } else {
            Parent->Balance++;
        }

        if (Parent->Balance == 0) {
            break;
        }

        if (Parent->Balance == -2) {

            if (Current->Balance == 1) {
                //
                // Left-right case: rotate Current left, then Parent right.
                //
                LONG SavedBalance;
                MiRotateLeft(Current, Root);
                NewSubRoot = MiRotateRight(Parent, Root);
                SavedBalance = NewSubRoot->Balance;
                NewSubRoot->LeftChild->Balance  = (SavedBalance == 1) ? -1 : 0;
                NewSubRoot->RightChild->Balance = (SavedBalance == -1) ? 1 : 0;
                NewSubRoot->Balance = 0;

            } else {
                //
                // Left-left case: rotate Parent right.
                //
                NewSubRoot = MiRotateRight(Parent, Root);
                NewSubRoot->Balance        = 0;
                NewSubRoot->RightChild->Balance = 0;
            }

            break;
        }

        if (Parent->Balance == 2) {

            if (Current->Balance == -1) {
                //
                // Right-left case: rotate Current right, then Parent left.
                //
                LONG SavedBalance;
                MiRotateRight(Current, Root);
                NewSubRoot = MiRotateLeft(Parent, Root);
                SavedBalance = NewSubRoot->Balance;
                NewSubRoot->LeftChild->Balance  = (SavedBalance == 1) ? -1 : 0;
                NewSubRoot->RightChild->Balance = (SavedBalance == -1) ? 1 : 0;
                NewSubRoot->Balance = 0;

            } else {
                //
                // Right-right case: rotate Parent left.
                //
                NewSubRoot = MiRotateLeft(Parent, Root);
                NewSubRoot->Balance       = 0;
                NewSubRoot->LeftChild->Balance = 0;
            }

            break;
        }

        Current = Parent;
    }
}

/* -----------------------------------------------------------------------
 * MiRemoveNode
 * ----------------------------------------------------------------------- */

/*++

Routine Description:

    Removes Node from the AVL tree, rebalancing as necessary.

    If Node has two children, it is replaced by its in-order successor
    (leftmost node in the right sub-tree), then the successor is removed
    from its original position.

Arguments:

    Node - The VAD to remove.

    Root - Pointer to the root pointer of the tree.

Return Value:

    None.

--*/
VOID
MiRemoveNode(
    PMMVAD  Node,
    PMMVAD *Root
    )
{
    PMMVAD  Replacement;
    PMMVAD  Child;
    PMMVAD  FixStart;
    BOOLEAN FromLeft;
    PMMVAD  Current;
    PMMVAD  Parent;

    //
    // If Node has two children, find the in-order successor and swap it
    // into Node's position.
    //
    if (Node->LeftChild != NULL && Node->RightChild != NULL) {

        Replacement = Node->RightChild;
        while (Replacement->LeftChild != NULL) {
            Replacement = Replacement->LeftChild;
        }

        //
        // Copy successor's key into Node and then remove the successor.
        //
        Node->StartingVpn = Replacement->StartingVpn;
        Node->EndingVpn   = Replacement->EndingVpn;
        Node->u.LongFlags = Replacement->u.LongFlags;
        Node = Replacement;
    }

    //
    // Node now has at most one child.
    //
    Child    = (Node->LeftChild != NULL) ? Node->LeftChild : Node->RightChild;
    FixStart = Node->Parent;

    if (FixStart == NULL) {
        //
        // Removing the root.
        //
        *Root = Child;
        if (Child != NULL) {
            Child->Parent = NULL;
        }
        return;
    }

    FromLeft = (FixStart->LeftChild == Node);

    if (FromLeft) {
        FixStart->LeftChild = Child;
    } else {
        FixStart->RightChild = Child;
    }

    if (Child != NULL) {
        Child->Parent = FixStart;
    }

    //
    // Rebalance walking upward.
    //
    Current = FixStart;

    while (Current != NULL) {

        if (FromLeft) {
            Current->Balance++;
        } else {
            Current->Balance--;
        }

        Parent = Current->Parent;

        if (Current->Balance == -2) {
            PMMVAD LeftChild = Current->LeftChild;

            if (LeftChild->Balance <= 0) {
                MiRotateRight(Current, Root);
                if (LeftChild->Balance == 0) {
                    LeftChild->Balance   = 1;
                    Current->Balance     = -1;
                    break;
                }
                LeftChild->Balance = 0;
                Current->Balance   = 0;
                Current = LeftChild;
            } else {
                PMMVAD LR = LeftChild->RightChild;
                MiRotateLeft(LeftChild, Root);
                MiRotateRight(Current, Root);
                if (LR->Balance == -1) {
                    LeftChild->Balance = 0;
                    Current->Balance   = 1;
                } else if (LR->Balance == 0) {
                    LeftChild->Balance = 0;
                    Current->Balance   = 0;
                } else {
                    LeftChild->Balance = -1;
                    Current->Balance   = 0;
                }
                LR->Balance = 0;
                Current = LR;
            }

        } else if (Current->Balance == 2) {
            PMMVAD RightChild = Current->RightChild;

            if (RightChild->Balance >= 0) {
                MiRotateLeft(Current, Root);
                if (RightChild->Balance == 0) {
                    RightChild->Balance = -1;
                    Current->Balance    = 1;
                    break;
                }
                RightChild->Balance = 0;
                Current->Balance    = 0;
                Current = RightChild;
            } else {
                PMMVAD RL = RightChild->LeftChild;
                MiRotateRight(RightChild, Root);
                MiRotateLeft(Current, Root);
                if (RL->Balance == 1) {
                    RightChild->Balance = 0;
                    Current->Balance    = -1;
                } else if (RL->Balance == 0) {
                    RightChild->Balance = 0;
                    Current->Balance    = 0;
                } else {
                    RightChild->Balance = 1;
                    Current->Balance    = 0;
                }
                RL->Balance = 0;
                Current = RL;
            }

        } else if (Current->Balance != 0) {
            break;
        }

        if (Parent != NULL) {
            FromLeft = (Parent->LeftChild == Current);
        }

        Current = Parent;
    }
}
